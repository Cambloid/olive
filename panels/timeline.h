#ifndef TIMELINE_H
#define TIMELINE_H

#include "ui/timelinetools.h"
#include <QDockWidget>
#include <QVector>
#include <QTime>
#include <QTimer>

class QPushButton;
class SourceTable;
class ViewerWidget;
struct Sequence;
struct Clip;
struct Media;
struct MediaStream;

struct Ghost {
    int clip;
	long in;
	long out;
	int track;
	long clip_in;

	long old_in;
	long old_out;
	int old_track;
	long old_clip_in;

	// importing variables
	Media* media;
	MediaStream* media_stream;

	// other variables
	long ghost_length;
	long media_length;
};

struct Selection {
	long in;
	long out;
	int track;

	long old_in;
	long old_out;
	int old_track;
};

namespace Ui {
class Timeline;
}

class Timeline : public QDockWidget
{
	Q_OBJECT

public:
	explicit Timeline(QWidget *parent = 0);
	~Timeline();

    bool focused();
    void set_zoom(bool in);
    void undo();
    void redo();
    void copy(bool del);
    void paste();
    void deselect();
    bool split_selection();
    void split_at_playhead();
    void split_clip_and_relink(int clip, long frame, bool relink);
    void clean_up_selections(QVector<Selection>& areas);
    void delete_areas_and_relink(QVector<Selection>& areas);
    void relink_clips_using_ids(QVector<int>& old_clips, QVector<int>& new_clips);
    void update_sequence();
    void increase_track_height();
    void decrease_track_height();
    void add_transition();

    int get_snap_range();
    int getScreenPointFromFrame(long frame);
    long getFrameFromScreenPoint(int x);

    bool snap_to_point(long point, long* l);
    void snap_to_clip(long* l, bool playhead_inclusive);

	long playhead;

	// playback functions
	void go_to_start();
	void previous_frame();
	void next_frame();
    void previous_cut();
    void next_cut();
	void seek(long p);
    void toggle_play();
	void play();
	void pause();
	void go_to_end();
	bool playing;
	long playhead_start;
    qint64 start_msecs;
	QTimer playback_updater;

	// shared information
    bool select_also_seeks;
    bool edit_tool_selects_links;
    bool edit_tool_also_seeks;
    bool paste_seeks;
	int tool;
    long cursor_frame;
    int cursor_track;
	float zoom;
	long drag_frame_start;
	int drag_track_start;
    void redraw_all_clips(bool changed);

    QVector<int> video_track_heights;
    QVector<int> audio_track_heights;
    int get_track_height_size(bool video);
    int calculate_track_height(int track, int height);

    // snapping
    bool snapping;
    bool snapped;
    long snap_point;

	// selecting functions
	bool selecting;
    int selection_offset;
	QVector<Selection> selections;
    bool is_clip_selected(Clip* clip);
	void delete_selection(bool ripple);
	void select_all();
    bool rect_select_init;
    bool rect_select_proc;
    int rect_select_x;
    int rect_select_y;
    int rect_select_w;
    int rect_select_h;

	// moving
	bool moving_init;
	bool moving_proc;
	QVector<Ghost> ghosts;

	// trimming
	int trim_target;
	bool trim_in;

	// splitting
	bool splitting;

	// importing
	bool importing;

	// ripple
	void ripple(long ripple_point, long ripple_length);

    Ui::Timeline *ui;
public slots:
	void repaint_timeline();

private slots:
	void on_toolEditButton_toggled(bool checked);

	void on_toolArrowButton_toggled(bool checked);

	void on_toolRazorButton_toggled(bool checked);

	void on_pushButton_4_clicked();

	void on_pushButton_5_clicked();

	void on_toolRippleButton_toggled(bool checked);

	void on_toolRollingButton_toggled(bool checked);

	void on_toolSlipButton_toggled(bool checked);

    void on_snappingButton_toggled(bool checked);

private:
	QVector<QPushButton*> tool_buttons;
	void decheck_tool_buttons(QObject* sender);
	void set_tool(int tool);
	long last_frame;
    QVector<Clip*> clip_clipboard;
    void reset_all_audio();
};

#endif // TIMELINE_H