#include "audio.h"

#include "project/sequence.h"

#include "io/config.h"
#include "panels/project.h"
#include "panels/panels.h"
#include "panels/timeline.h"
#include "panels/viewer.h"
#include "ui/audiomonitor.h"
#include "playback/playback.h"
#include "debug.h"

#include <QApplication>
#include <QAudioOutput>
#include <QAudioInput>
#include <QtMath>
#include <QFile>
#include <QDir>

extern "C" {
	#include <libavcodec/avcodec.h>
}

QAudioOutput* audio_output;
QIODevice* audio_io_device;
bool audio_device_set = false;
bool audio_scrub = false;
QMutex audio_write_lock;
QAudioInput* audio_input = nullptr;
QFile output_recording;
bool recording = false;

qint8 audio_ibuffer[audio_ibuffer_size];
int audio_ibuffer_read = 0;
long audio_ibuffer_frame = 0;
double audio_ibuffer_timecode = 0;

AudioSenderThread* audio_thread = nullptr;

bool is_audio_device_set() {
	return audio_device_set;
}

void init_audio() {
	stop_audio();

	QAudioFormat audio_format;
	audio_format.setSampleRate(config.audio_rate);
	audio_format.setChannelCount(2);
	audio_format.setSampleSize(16);
	audio_format.setCodec("audio/pcm");
	audio_format.setByteOrder(QAudioFormat::LittleEndian);
	audio_format.setSampleType(QAudioFormat::SignedInt);

	QAudioDeviceInfo info(QAudioDeviceInfo::defaultOutputDevice());
	QList<QAudioDeviceInfo> devs = QAudioDeviceInfo::availableDevices(QAudio::AudioOutput);
	qInfo() << "Found the following audio devices:";
	for (int i=0;i<devs.size();i++) {
		dout << "    " << devs.at(i).deviceName();
	}
	if (info.isNull() && devs.size() > 0) {
		qWarning() << "Default audio returned nullptr, attempting to use first device found...";
		info = devs.at(0);
	}
	qInfo() << "Using audio device" << info.deviceName();

	if (!info.isFormatSupported(audio_format)) {
		qWarning() << "Audio format is not supported by backend, using nearest";
		audio_format = info.nearestFormat(audio_format);
	}

	audio_output = new QAudioOutput(info, audio_format);
	audio_output->moveToThread(QApplication::instance()->thread());
	audio_output->setNotifyInterval(5);

	// connect
	audio_io_device = audio_output->start();
	if (audio_io_device == nullptr) {
		qWarning() << "Received nullptr audio device. No compatible audio output was found.";
	} else {
		audio_device_set = true;

		// start sender thread
		audio_thread = new AudioSenderThread();
		QObject::connect(audio_output, SIGNAL(notify()), audio_thread, SLOT(notifyReceiver()));
		audio_thread->start(QThread::TimeCriticalPriority);

		clear_audio_ibuffer();
	}
}

void stop_audio() {
	if (audio_device_set) {
		audio_thread->stop();

		audio_output->stop();
		delete audio_output;
		audio_device_set = false;
	}
}

void clear_audio_ibuffer() {
	if (audio_thread != nullptr) audio_thread->lock.lock();
	memset(audio_ibuffer, 0, audio_ibuffer_size);
	audio_ibuffer_read = 0;
	if (audio_thread != nullptr) audio_thread->lock.unlock();
}

int current_audio_freq() {
	return rendering ? sequence->audio_frequency : audio_output->format().sampleRate();
}

int get_buffer_offset_from_frame(double framerate, long frame) {
	if (frame >= audio_ibuffer_frame) {
		return qFloor(((double) (frame - audio_ibuffer_frame)/framerate)*current_audio_freq())*av_get_bytes_per_sample(AV_SAMPLE_FMT_S16)*av_get_channel_layout_nb_channels(AV_CH_LAYOUT_STEREO);
	} else {
		qWarning() << "Invalid values passed to get_buffer_offset_from_frame";
		return 0;
	}
}

AudioSenderThread::AudioSenderThread() : close(false) {
	connect(this, SIGNAL(finished()), this, SLOT(deleteLater()));
}

void AudioSenderThread::stop() {
	close = true;
	cond.wakeAll();
	wait();
}

void AudioSenderThread::notifyReceiver() {
	cond.wakeAll();
}

void AudioSenderThread::run() {
	// start data loop
	send_audio_to_output(0, audio_ibuffer_size);

	lock.lock();
	while (true) {
		cond.wait(&lock);
		if (close) {
			break;
		} else if (panel_sequence_viewer->playing || panel_footage_viewer->playing || audio_scrub) {
			int written_bytes = 0;

			int adjusted_read_index = audio_ibuffer_read%audio_ibuffer_size;
			int max_write = audio_ibuffer_size - adjusted_read_index;
			int actual_write = send_audio_to_output(adjusted_read_index, max_write);
			written_bytes += actual_write;
			if (actual_write == max_write) {
				// got all the bytes, write again
				written_bytes += send_audio_to_output(0, audio_ibuffer_size);
			}

			audio_scrub = false;
		}
	}
	lock.unlock();
}

int AudioSenderThread::send_audio_to_output(int offset, int max) {
	// send audio to device
	int actual_write = audio_io_device->write((const char*) audio_ibuffer+offset, max);

	int audio_ibuffer_limit = audio_ibuffer_read + actual_write;

	// send samples to audio monitor cache
	// TODO make this work for the footage viewer - currently, enabling it causes crash due to an ASSERT
	Sequence* s = nullptr;
	/*if (panel_footage_viewer->playing) {
		s = panel_footage_viewer->seq;
	}*/
	if (panel_sequence_viewer->playing) {
		s = panel_sequence_viewer->seq;
	}
	if (s != nullptr) {
		if (panel_timeline->audio_monitor->sample_cache_offset == -1) {
			panel_timeline->audio_monitor->sample_cache_offset = s->playhead;
		}
		int channel_count = av_get_channel_layout_nb_channels(s->audio_layout);
		long sample_cache_playhead = panel_timeline->audio_monitor->sample_cache_offset + (panel_timeline->audio_monitor->sample_cache.size()/channel_count);
		int next_buffer_offset, buffer_offset_adjusted, i;
		int buffer_offset = get_buffer_offset_from_frame(s->frame_rate, sample_cache_playhead);
		if (samples.size() != channel_count) samples.resize(channel_count);
		samples.fill(0);

		// TODO: I don't like this, but i'm not sure if there's a smarter way to do it
		while (buffer_offset < audio_ibuffer_limit) {
			sample_cache_playhead++;
			next_buffer_offset = qMin(get_buffer_offset_from_frame(s->frame_rate, sample_cache_playhead), audio_ibuffer_limit);
			while (buffer_offset < next_buffer_offset) {
				for (i=0;i<samples.size();i++) {
					buffer_offset_adjusted = buffer_offset%audio_ibuffer_size;
					samples[i] = qMax(qAbs((qint16) (((audio_ibuffer[buffer_offset_adjusted+1] & 0xFF) << 8) | (audio_ibuffer[buffer_offset_adjusted] & 0xFF))), samples[i]);
					buffer_offset += 2;
				}
			}
			panel_timeline->audio_monitor->sample_cache.append(samples);
			buffer_offset = next_buffer_offset;
		}
	}

	memset(audio_ibuffer+offset, 0, actual_write);

	audio_ibuffer_read = audio_ibuffer_limit;

	return actual_write;
}

void int32_to_char_array(qint32 i, char* array) {
	memcpy(array, &i, 4);
}

void write_wave_header(QFile& f, const QAudioFormat& format) {
	qint32 int32bit;
	char arr[4];

	// 4 byte riff header
	f.write("RIFF");

	// 4 byte file size, filled in later
	for (int i=0;i<4;i++) f.putChar(0);

	// 4 byte file type header + 4 byte format chunk marker
	f.write("WAVEfmt");
	f.putChar(0x20);

	// 4 byte length of the above format data (always 16 bytes)
	f.putChar(16);
	for (int i=0;i<3;i++) f.putChar(0);

	// 2 byte type format (1 is PCM)
	f.putChar(1);
	f.putChar(0);

	// 2 byte channel count
	int32bit = format.channelCount();
	int32_to_char_array(int32bit, arr);
	f.write(arr, 2);

	// 4 byte integer for sample rate
	int32bit = format.sampleRate();
	int32_to_char_array(int32bit, arr);
	f.write(arr, 4);

	// 4 byte integer for bytes per second
	int32bit = (format.sampleRate() * format.sampleSize() * format.channelCount()) / 8;
	int32_to_char_array(int32bit, arr);
	f.write(arr, 4);

	// 2 byte integer for bytes per sample per channel
	int32bit = (format.sampleSize() * format.channelCount()) / 8;
	int32_to_char_array(int32bit, arr);
	f.write(arr, 2);

	// 2 byte integer for bits per sample (16)
	int32bit = format.sampleSize();
	int32_to_char_array(int32bit, arr);
	f.write(arr, 2);

	// data chunk header
	f.write("data");

	// 4 byte integer for data chunk size (filled in later)?
	for (int i=0;i<4;i++) f.putChar(0);
}

void write_wave_trailer(QFile& f) {
	char arr[4];

	f.seek(4);

	// 4 bytes for total file size - 8 bytes
	qint32 file_size = f.size() - 8;
	int32_to_char_array(file_size, arr);
	f.write(arr, 4);

	f.seek(40);

	// 4 bytes for data chunk size (file size - header)
	file_size = f.size() - 44;
	int32_to_char_array(file_size, arr);
	f.write(arr, 4);
}

bool start_recording() {
	if (sequence == nullptr) {
		qCritical() << "No active sequence to record into";
		return false;
	}

    QString audio_path = project_url + " " + QCoreApplication::translate("Audio", "Audio");
	QDir audio_dir(audio_path);
	if (!audio_dir.exists() && !audio_dir.mkpath(".")) {
		qCritical() << "Failed to create audio directory";
		return false;
	}

	QString audio_filename;
	int file_number = 0;
	do {
		file_number++;
        audio_filename = audio_path + "/" + QCoreApplication::translate("Audio", "Recording") + " " + QString::number(file_number) + ".wav";
	} while (QFile(audio_filename).exists());

	output_recording.setFileName(audio_filename);
	if (!output_recording.open(QFile::WriteOnly)) {
		qCritical() << "Failed to open output file. Does Olive have permission to write to this directory?";
		return false;
	}

	QAudioFormat audio_format = audio_output->format();
	if (config.recording_mode != audio_format.channelCount()) {
		audio_format.setChannelCount(config.recording_mode);
	}
	QAudioDeviceInfo info = QAudioDeviceInfo::defaultInputDevice();
	if (!info.isFormatSupported(audio_format)) {
		qWarning() << "Default format not supported, using nearest";
		audio_format = info.nearestFormat(audio_format);
	}
	write_wave_header(output_recording, audio_format);
	audio_input = new QAudioInput(audio_format);
	audio_input->start(&output_recording);
	recording = true;

	return true;
}

void stop_recording() {
	if (recording) {
		audio_input->stop();

		write_wave_trailer(output_recording);

		output_recording.close();

		delete audio_input;
		audio_input = nullptr;
		recording = false;
	}
}

QString get_recorded_audio_filename() {
	return output_recording.fileName();
}
