#include "timelinewidget.h"

#include "panels/panels.h"
#include "io/config.h"
#include "project/sequence.h"
#include "project/clip.h"
#include "panels/project.h"
#include "panels/timeline.h"
#include "io/media.h"
#include "ui/sourcetable.h"
#include "panels/effectcontrols.h"

#include "effects/effects.h"
#include "effects/transition.h"

#include <QPainter>
#include <QColor>
#include <QDebug>
#include <QMouseEvent>
#include <QObject>
#include <QVariant>
#include <QPoint>
#include <QMenu>
#include <QtMath>

#define MAX_TEXT_WIDTH 20

TimelineWidget::TimelineWidget(QWidget *parent) : QWidget(parent) {
	bottom_align = false;
    track_resizing = false;
    setMouseTracking(true);

    setAcceptDrops(true);

    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, SIGNAL(customContextMenuRequested(const QPoint&)), this, SLOT(show_context_menu(const QPoint&)));
}

void TimelineWidget::show_context_menu(const QPoint& pos) {
    QMenu menu(this);
    menu.addAction("heck!");
    menu.exec(mapToGlobal(pos));
}

bool same_sign(int a, int b) {
	return (a < 0) == (b < 0);
}

void TimelineWidget::resizeEvent(QResizeEvent*) {
    if (sequence != NULL) redraw_clips();
}

void TimelineWidget::dragEnterEvent(QDragEnterEvent *event) {
	if (static_cast<SourceTable*>(event->source()) == panel_project->source_table) {
		QPoint pos = event->pos();
		event->accept();
		QList<QTreeWidgetItem*> items = panel_project->source_table->selectedItems();
        long entry_point = panel_timeline->getFrameFromScreenPoint(pos.x());
        panel_timeline->drag_frame_start = entry_point + panel_timeline->getFrameFromScreenPoint(50);
		panel_timeline->drag_track_start = (bottom_align) ? -1 : 0;
		for (int i=0;i<items.size();i++) {
			bool ignore_infinite_length = false;
            Media* m = panel_project->get_media_from_tree(items.at(i));
            long duration = m->get_length_in_frames(sequence->frame_rate);
            Ghost g = {0, entry_point, entry_point + duration};
			g.media = m;
			g.clip_in = 0;
			for (int j=0;j<m->audio_tracks.size();j++) {
				g.track = j;
                g.media_stream = m->audio_tracks.at(j);
				ignore_infinite_length = true;
				panel_timeline->ghosts.append(g);
			}
			for (int j=0;j<m->video_tracks.size();j++) {
				g.track = -1-j;
                g.media_stream = m->video_tracks.at(j);
                if (m->video_tracks[j]->infinite_length && !ignore_infinite_length) g.out = g.in + 100;
				panel_timeline->ghosts.append(g);
			}
			entry_point += duration;
		}
		init_ghosts();
		panel_timeline->importing = true;
	}
}

void TimelineWidget::dragMoveEvent(QDragMoveEvent *event) {
	if (panel_timeline->importing) {
		QPoint pos = event->pos();
		update_ghosts(pos);
		panel_timeline->repaint_timeline();
	}
}

void TimelineWidget::dragLeaveEvent(QDragLeaveEvent*) {
	if (panel_timeline->importing) {
		panel_timeline->ghosts.clear();
		panel_timeline->importing = false;
		panel_timeline->repaint_timeline();
	}
}

void TimelineWidget::dropEvent(QDropEvent* event) {
	if (panel_timeline->importing) {
		event->accept();

        QVector<int> added_clips;

        // delete areas before adding
        QVector<Selection> delete_areas;
        for (int i=0;i<panel_timeline->ghosts.size();i++) {
            const Ghost& g = panel_timeline->ghosts.at(i);
            Selection s;
            s.in = g.in;
            s.out = g.out;
            s.track = g.track;
            delete_areas.append(s);
        }
        panel_timeline->delete_areas_and_relink(delete_areas);

        // add clips
		for (int i=0;i<panel_timeline->ghosts.size();i++) {
            const Ghost& g = panel_timeline->ghosts.at(i);

            Clip* c = new Clip();
            c->media = g.media;
            c->media_stream = g.media_stream;
            c->timeline_in = g.in;
            c->timeline_out = g.out;
            c->clip_in = g.clip_in;
            c->color_r = 128;
            c->color_g = 128;
            c->color_b = 192;
            c->sequence = sequence;
            c->track = g.track;
            c->name = c->media->name;

            if (c->track < 0) {
				// add default video effects
                c->effects.append(create_effect(VIDEO_TRANSFORM_EFFECT, c));
			} else {
				// add default audio effects
                c->effects.append(create_effect(AUDIO_VOLUME_EFFECT, c));
                c->effects.append(create_effect(AUDIO_PAN_EFFECT, c));
            }

            added_clips.append(sequence->add_clip(c));
		}

        // link clips from the same media
        for (int i=0;i<added_clips.size();i++) {
            Clip* c = sequence->get_clip(added_clips.at(i));
            for (int j=0;j<added_clips.size();j++) {
                Clip* cc = sequence->get_clip(added_clips.at(j));
                if (c != cc && c->media == cc->media) {
                    c->linked.append(added_clips.at(j));
                }
            }
        }

		panel_timeline->ghosts.clear();
		panel_timeline->importing = false;
        panel_timeline->snapped = false;

        setFocus();

        panel_timeline->redraw_all_clips(true);
	}
}

void TimelineWidget::mouseDoubleClickEvent(QMouseEvent *event) {
    if (panel_timeline->tool == TIMELINE_TOOL_EDIT) {
        int clip_index = getClipIndexFromCoords(panel_timeline->cursor_frame, panel_timeline->cursor_track);
        if (clip_index >= 0) {
            Clip* clip = sequence->get_clip(clip_index);
            if (!(event->modifiers() & Qt::ShiftModifier)) panel_timeline->selections.clear();
            Selection s;
            s.in = clip->timeline_in;
            s.out = clip->timeline_out;
            s.track = clip->track;
            panel_timeline->selections.append(s);
            panel_timeline->repaint_timeline();
        }
    }
}

void TimelineWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        if (panel_timeline->tool == TIMELINE_TOOL_EDIT || panel_timeline->tool == TIMELINE_TOOL_RAZOR) {
            panel_timeline->drag_frame_start = panel_timeline->cursor_frame;
            panel_timeline->drag_track_start = panel_timeline->cursor_track;
        } else {
            QPoint pos = event->pos();
            panel_timeline->drag_frame_start = panel_timeline->getFrameFromScreenPoint(pos.x());
            panel_timeline->drag_track_start = getTrackFromScreenPoint(pos.y());
        }

        int clip_index = panel_timeline->trim_target;
        if (clip_index == -1) clip_index = getClipIndexFromCoords(panel_timeline->drag_frame_start, panel_timeline->drag_track_start);

        bool shift = (event->modifiers() & Qt::ShiftModifier);

        if (shift) {
            panel_timeline->selection_offset = panel_timeline->selections.size();
        } else {
            panel_timeline->selection_offset = 0;
        }

        switch (panel_timeline->tool) {
        case TIMELINE_TOOL_POINTER:
        case TIMELINE_TOOL_RIPPLE:
        case TIMELINE_TOOL_SLIP:
        {
            if (track_resizing) {
                track_resize_mouse_cache = event->pos().y();
                panel_timeline->moving_init = true;
            } else {
                if (clip_index >= 0) {
                    Clip* clip = sequence->get_clip(clip_index);
                    if (clip != NULL) {
                        if (panel_timeline->is_clip_selected(clip)) {
                            if (shift) {
                                // TODO if shift is down, deselect it
                            }
                        } else {
                            // if "shift" is not down
                            if (!shift) {
                                panel_timeline->selections.clear();
                            }

                            Selection s;
                            s.in = clip->timeline_in;
                            s.out = clip->timeline_out;
                            s.track = clip->track;
                            panel_timeline->selections.append(s);

                            if (panel_timeline->select_also_seeks) {
                                panel_timeline->seek(clip->timeline_in);
                            }

                            // if alt is not down, select links
                            if (!(event->modifiers() & Qt::AltModifier)) {
                                for (int i=0;i<clip->linked.size();i++) {
                                    Clip* link = sequence->get_clip(clip->linked.at(i));
                                    if (!panel_timeline->is_clip_selected(link)) {
                                        Selection ss;
                                        ss.in = link->timeline_in;
                                        ss.out = link->timeline_out;
                                        ss.track = link->track;
                                        panel_timeline->selections.append(ss);
                                    }
                                }
                            }
                        }
                    }

                    panel_timeline->moving_init = true;
                } else {
                    // if "shift" is not down
                    if (!shift) {
                        panel_timeline->selections.clear();
                    }

                    panel_timeline->rect_select_init = true;
                }
                panel_timeline->repaint_timeline();
            }
        }
            break;
        case TIMELINE_TOOL_EDIT:
            if (panel_timeline->edit_tool_also_seeks) panel_timeline->seek(panel_timeline->drag_frame_start);
            panel_timeline->selecting = true;
            break;
        case TIMELINE_TOOL_RAZOR:
        {
            if (clip_index >= 0) {
                panel_timeline->split_clip_and_relink(clip_index, panel_timeline->drag_frame_start, !(event->modifiers() & Qt::AltModifier));
            }
            panel_timeline->splitting = true;
            panel_timeline->redraw_all_clips(true);
        }
            break;
        }
    }
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        bool repaint = false;

        if (panel_timeline->moving_proc) {
            // if we were RIPPLING, move all the clips
            if (panel_timeline->tool == TIMELINE_TOOL_RIPPLE) {
                long ripple_length, ripple_point;

                // ripple_length becomes the length/number of frames we trimmed
                // ripple point becomes the point to ripple (i.e. the point after or before which we move every clip)
                if (panel_timeline->trim_in) {
                    ripple_length = panel_timeline->ghosts.at(0).old_in - panel_timeline->ghosts.at(0).in;
                    ripple_point = panel_timeline->ghosts.at(0).old_in;
                } else {
                    // if we're trimming an out-point
                    ripple_length = panel_timeline->ghosts.at(0).old_out - panel_timeline->ghosts.at(0).out;
                    ripple_point = panel_timeline->ghosts.at(0).old_out;
                }
                for (int i=0;i<panel_timeline->ghosts.size();i++) {
                    long comp_point;
                    if (panel_timeline->trim_in) {
                        comp_point = panel_timeline->ghosts.at(i).old_in;
                    } else {
                        comp_point = panel_timeline->ghosts.at(i).old_out;
                    }
                    if (ripple_point > comp_point) ripple_point = comp_point;
                }
                if (!panel_timeline->trim_in) ripple_length = -ripple_length;
                panel_timeline->ripple(ripple_point, ripple_length);
            }

            if (panel_timeline->tool == TIMELINE_TOOL_POINTER && (event->modifiers() & Qt::AltModifier)) { // if holding alt, duplicate rather than move
                // duplicate clips
                QVector<Clip*> copy_clips;
                QVector<int> old_clips;
                QVector<int> new_clips;
                QVector<Selection> delete_areas;
                for (int i=0;i<panel_timeline->ghosts.size();i++) {
                    const Ghost& g = panel_timeline->ghosts.at(i);
                    if (g.old_in != g.in || g.old_out != g.out || g.track != g.old_track || g.clip_in != g.old_clip_in) {
                        Clip* c = sequence->get_clip(g.clip)->copy();

                        c->timeline_in = g.in;
                        c->timeline_out = g.out;
                        c->track = g.track;

                        Selection s;
                        s.in = g.in;
                        s.out = g.out;
                        s.track = g.track;
                        delete_areas.append(s);

                        old_clips.append(g.clip);
                        copy_clips.append(c);
                    }
                }
                if (copy_clips.size() > 0) {
                    panel_timeline->delete_areas_and_relink(delete_areas);
                    for (int i=0;i<copy_clips.size();i++) {
                        // step 2 - delete anything that exists in area that clip is moving to
                        new_clips.append(sequence->add_clip(copy_clips.at(i)));
                    }
                    // relink duplicated clips
                    panel_timeline->relink_clips_using_ids(old_clips, new_clips);
                }
            } else {
                // move clips
                // TODO can we do this better than 3 consecutive for loops?
                if (panel_timeline->tool == TIMELINE_TOOL_POINTER) {
                    for (int i=0;i<panel_timeline->ghosts.size();i++) {
                        // step 1 - set clips that are moving to "undeletable" (to avoid step 2 deleting any part of them)
                        sequence->get_clip(panel_timeline->ghosts[i].clip)->undeletable = true;
                    }
                    // step 2 - delete areas
                    QVector<Selection> delete_areas;
                    for (int i=0;i<panel_timeline->ghosts.size();i++) {
                        // step 2 - delete anything that exists in area that clip is moving to
                        // note: ripples are non-destructive so this is pointer-tool exclusive
                        const Ghost& g = panel_timeline->ghosts.at(i);
                        Selection s;
                        s.in = g.in;
                        s.out = g.out;
                        s.track = g.track;
                        delete_areas.append(s);
                    }
                    panel_timeline->delete_areas_and_relink(delete_areas);
                }
                for (int i=0;i<panel_timeline->ghosts.size();i++) {
                    Ghost& g = panel_timeline->ghosts[i];

                    // step 3 - move clips
                    Clip* c = sequence->get_clip(g.clip);
                    c->timeline_in += (g.in - g.old_in);
                    c->timeline_out += (g.out - g.old_out);
                    c->track += (g.track - g.old_track);
                    c->clip_in += (g.clip_in - g.old_clip_in);
                }
                if (panel_timeline->tool == TIMELINE_TOOL_POINTER) {
                    for (int i=0;i<panel_timeline->ghosts.size();i++) {
                        // step 4 - set clips back to deletable
                        sequence->get_clip(panel_timeline->ghosts[i].clip)->undeletable = false;
                    }
                }
            }

            panel_timeline->redraw_all_clips(true);
        } else if (panel_timeline->selecting) {
            // remove duplicate selections
            panel_timeline->clean_up_selections(panel_timeline->selections);
            repaint = true;
        } else if (panel_timeline->rect_select_proc) {
            repaint = true;
        }

        // destroy all ghosts
        panel_timeline->ghosts.clear();

        panel_timeline->selecting = false;
        panel_timeline->moving_proc = false;
        panel_timeline->moving_init = false;
        panel_timeline->splitting = false;
        panel_timeline->snapped = false;
        panel_timeline->rect_select_init = false;
        panel_timeline->rect_select_proc = false;
        pre_clips.clear();
        post_clips.clear();

        if (repaint) panel_timeline->repaint_timeline();

        // SEND CLIPS TO EFFECT CONTROLS
        // find out how many clips are selected
        // limits to one video clip and one audio clip and only if they're linked
        // one of these days it might be nice to have multiple clips in the effects panel
        bool got_vclip = false;
        bool got_aclip = false;
        int vclip = -1;
        int aclip = -1;
        for (int i=0;i<sequence->clip_count();i++) {
            Clip* clip = sequence->get_clip(i);
            if (clip != NULL && panel_timeline->is_clip_selected(clip)) {
                if (clip->track < 0) {
                    if (got_vclip) {
                        vclip = -1;
                    } else {
                        vclip = i;
                        got_vclip = true;
                    }
                } else {
                    if (got_aclip) {
                        aclip = -1;
                    } else {
                        aclip = i;
                        got_aclip = true;
                    }
                }
            }
        }
        // check if aclip is linked to vclip
        QVector<int> selected_clips;
        if (vclip != -1) selected_clips.append(vclip);
        if (aclip != -1) selected_clips.append(aclip);
        if (vclip != -1 && aclip != -1) {
            bool found = false;
            Clip* vclip_ref = sequence->get_clip(vclip);
            for (int i=0;i<vclip_ref->linked.size();i++) {
                if (vclip_ref->linked.at(i) == aclip) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                // only display multiple clips if they're linked
                selected_clips.clear();
            }
        }
        panel_effect_controls->set_clips(selected_clips);
    }
}

void TimelineWidget::init_ghosts() {
	for (int i=0;i<panel_timeline->ghosts.size();i++) {
		Ghost& g = panel_timeline->ghosts[i];
		g.old_in = g.in;
		g.old_out = g.out;
		g.old_track = g.track;
        g.old_clip_in = g.clip_in;

        if (panel_timeline->trim_target > -1 || panel_timeline->tool == TIMELINE_TOOL_SLIP) {
			// used for trim ops
			g.ghost_length = g.old_out - g.old_in;
            g.media_length = sequence->get_clip(g.clip)->media->get_length_in_frames(sequence->frame_rate);
		}
	}
	for (int i=0;i<panel_timeline->selections.size();i++) {
		Selection& s = panel_timeline->selections[i];
		s.old_in = s.in;
		s.old_out = s.out;
		s.old_track = s.track;
	}
}

bool subvalidate_snapping(Ghost& g, long* frame_diff, long snap_point) {
    int snap_range = panel_timeline->get_snap_range();
    long in_validator = g.old_in + *frame_diff - snap_point;
    long out_validator = g.old_out + *frame_diff - snap_point;

    if ((panel_timeline->trim_target == -1 || panel_timeline->trim_in) && in_validator > -snap_range && in_validator < snap_range) {
        *frame_diff -= in_validator;
        panel_timeline->snap_point = snap_point;
        panel_timeline->snapped = true;
        return true;
    } else if (!panel_timeline->trim_in && out_validator > -snap_range && out_validator < snap_range) {
        *frame_diff -= out_validator;
        panel_timeline->snap_point = snap_point;
        panel_timeline->snapped = true;
        return true;
    }
    return false;
}

void validate_snapping(Ghost& g, long* frame_diff) {
    panel_timeline->snapped = false;
    if (panel_timeline->snapping) {
        if (!subvalidate_snapping(g, frame_diff, panel_timeline->playhead)) {
            for (int j=0;j<sequence->clip_count();j++) {
                Clip* c = sequence->get_clip(j);
                if (c != NULL) {
                    if (!subvalidate_snapping(g, frame_diff, c->timeline_in)) {
                        subvalidate_snapping(g, frame_diff, c->timeline_out);
                    }
                    if (panel_timeline->snapped) break;
                }
            }
        }
    }
}

void TimelineWidget::update_ghosts(QPoint& mouse_pos) {
	int mouse_track = getTrackFromScreenPoint(mouse_pos.y());
    long frame_diff = panel_timeline->getFrameFromScreenPoint(mouse_pos.x()) - panel_timeline->drag_frame_start;
	int track_diff = mouse_track - panel_timeline->drag_track_start;

	long validator;
    if (panel_timeline->trim_target > -1) { // if trimming
		// trim ops

		// validate ghosts
		for (int i=0;i<panel_timeline->ghosts.size();i++) {
			Ghost& g = panel_timeline->ghosts[i];
            Clip* c = sequence->get_clip(g.clip);

            validate_snapping(g, &frame_diff);

			if (panel_timeline->trim_in) {
				// prevent clip length from being less than 1 frame long
				validator = g.ghost_length - frame_diff;
				if (validator < 1) frame_diff -= (1 - validator);

				// prevent timeline in from going below 0
				validator = g.old_in + frame_diff;
				if (validator < 0) frame_diff -= validator;

                if (!c->media_stream->infinite_length) {
					// prevent clip_in from going below 0
					validator = g.old_clip_in + frame_diff;
					if (validator < 0) frame_diff -= validator;
                }
			} else {
				// prevent clip length from being less than 1 frame long
				validator = g.ghost_length + frame_diff;
				if (validator < 1) frame_diff += (1 - validator);

                if (!c->media_stream->infinite_length) {
					// prevent clip length exceeding media length
					validator = g.ghost_length + frame_diff;
					if (validator > g.media_length) frame_diff -= validator - g.media_length;
                }
			}

            // ripple ops
            if (panel_timeline->tool == TIMELINE_TOOL_RIPPLE) {
                for (int j=0;j<post_clips.size();j++) {
                    // prevent any rippled clip from going below 0
                    Clip* post = post_clips.at(j);
                    validator = post->timeline_in - frame_diff;
                    if (validator < 0) frame_diff += validator;

                    // prevent any post-clips colliding with pre-clips
                    for (int k=0;k<pre_clips.size();k++) {
                        Clip* pre = pre_clips.at(k);
                        if (pre != post && pre->track == post->track) {
                            if (panel_timeline->trim_in) {
                                validator = post->timeline_in - frame_diff - pre->timeline_out;
                                if (validator < 0) frame_diff += validator;
                            } else {
                                validator = post->timeline_in + frame_diff - pre->timeline_out;
                                if (validator < 0) frame_diff -= validator;
                            }
                        }
                    }
                }
            }
        }

        // resize ghosts
        for (int i=0;i<panel_timeline->ghosts.size();i++) {
            Ghost& g = panel_timeline->ghosts[i];

            if (panel_timeline->trim_in) {
                g.in = g.old_in + frame_diff;
                g.clip_in = g.old_clip_in + frame_diff;
            } else {
                g.out = g.old_out + frame_diff;
            }
        }

        // resize selections
        for (int i=0;i<panel_timeline->selections.size();i++) {
            Selection& s = panel_timeline->selections[i];

            if (panel_timeline->trim_in) {
                s.in = s.old_in + frame_diff;
            } else {
                s.out = s.old_out + frame_diff;
            }
        }
	} else if (panel_timeline->tool == TIMELINE_TOOL_POINTER || panel_timeline->importing) { // only move clips on pointer (not ripple or rolling)
		// validate ghosts
		for (int i=0;i<panel_timeline->ghosts.size();i++) {
			Ghost& g = panel_timeline->ghosts[i];

			// prevent clips from moving below 0 on the timeline
			validator = g.old_in + frame_diff;
			if (validator < 0) frame_diff -= validator;

			// prevent clips from crossing tracks
			if (same_sign(g.old_track, panel_timeline->drag_track_start)) {
				while (!same_sign(g.old_track, g.old_track + track_diff)) {
					if (g.old_track < 0) {
						track_diff--;
					} else {
						track_diff++;
					}
				}
			}

            validate_snapping(g, &frame_diff);
        }

		// move ghosts
		for (int i=0;i<panel_timeline->ghosts.size();i++) {
			Ghost& g = panel_timeline->ghosts[i];
			g.in = g.old_in + frame_diff;
			g.out = g.old_out + frame_diff;

			g.track = g.old_track;

			if (panel_timeline->importing) {
				int abs_track_diff = abs(track_diff);
				if (g.old_track < 0) { // clip is video
					g.track -= abs_track_diff;
				} else { // clip is audio
					g.track += abs_track_diff;
				}
			} else {
				if (same_sign(g.old_track, panel_timeline->drag_track_start)) g.track += track_diff;
			}
		}

		// move selections
		if (!panel_timeline->importing) {
			for (int i=0;i<panel_timeline->selections.size();i++) {
				Selection& s = panel_timeline->selections[i];
				s.in = s.old_in + frame_diff;
				s.out = s.old_out + frame_diff;
				s.track = s.old_track;
				if (panel_timeline->importing) {
					int abs_track_diff = abs(track_diff);
					if (s.old_track < 0) {
						s.track -= abs_track_diff;
					} else {
						s.track += abs_track_diff;
					}
				} else {
					if (same_sign(s.track, panel_timeline->drag_track_start)) s.track += track_diff;
				}
			}
		}
    } else if (panel_timeline->tool == TIMELINE_TOOL_SLIP) {
        // validate ghosts
        for (int i=0;i<panel_timeline->ghosts.size();i++) {
            Ghost& g = panel_timeline->ghosts[i];
            if (!sequence->get_clip(g.clip)->media_stream->infinite_length) {
                // prevent slip moving a clip below 0 clip_in
                validator = g.old_clip_in - frame_diff;
                if (validator < 0) frame_diff += validator;

                // prevent slip moving clip beyond media length
                validator += g.ghost_length;
                if (validator > g.media_length) frame_diff += validator - g.media_length;
            }
        }

        // slip ghosts
        for (int i=0;i<panel_timeline->ghosts.size();i++) {
            Ghost& g = panel_timeline->ghosts[i];

            g.clip_in = g.old_clip_in - frame_diff;
        }
    }
}

void TimelineWidget::mouseMoveEvent(QMouseEvent *event) {
    bool alt = (event->modifiers() & Qt::AltModifier);

    panel_timeline->cursor_frame = panel_timeline->getFrameFromScreenPoint(event->pos().x());
    panel_timeline->cursor_track = getTrackFromScreenPoint(event->pos().y());

    if (panel_timeline->tool == TIMELINE_TOOL_EDIT || panel_timeline->tool == TIMELINE_TOOL_RAZOR) {
        panel_timeline->snap_to_clip(&panel_timeline->cursor_frame, !panel_timeline->edit_tool_also_seeks || !panel_timeline->selecting);
    }
    if (panel_timeline->selecting) {
        int selection_count = 1 + qMax(panel_timeline->cursor_track, panel_timeline->drag_track_start) - qMin(panel_timeline->cursor_track, panel_timeline->drag_track_start) + panel_timeline->selection_offset;
		if (panel_timeline->selections.size() != selection_count) {
			panel_timeline->selections.resize(selection_count);
		}
        int minimum_selection_track = qMin(panel_timeline->cursor_track, panel_timeline->drag_track_start);
        for (int i=panel_timeline->selection_offset;i<selection_count;i++) {
            Selection& s = panel_timeline->selections[i];
            s.track = minimum_selection_track + i - panel_timeline->selection_offset;
			long in = panel_timeline->drag_frame_start;
            long out = panel_timeline->cursor_frame;
            s.in = qMin(in, out);
            s.out = qMax(in, out);
		}

        // select linked clips too
        if (panel_timeline->edit_tool_selects_links) {
            for (int j=0;j<sequence->clip_count();j++) {
                Clip* c = sequence->get_clip(j);
                for (int i=0;i<panel_timeline->selections.size();i++) {
                    const Selection& s = panel_timeline->selections.at(i);
                    if (c->track == s.track &&
                            !(c->timeline_in < s.in && c->timeline_out < s.in) &&
                            !(c->timeline_in > s.out && c->timeline_out > s.out)) {
                        for (int k=0;k<c->linked.size();k++) {
                            Clip* link = sequence->get_clip(c->linked.at(k));
                            bool found = false;
                            for (int l=0;l<panel_timeline->selections.size();l++) {
                                const Selection& test_sel = panel_timeline->selections.at(l);
                                if (test_sel.track == link->track &&
                                        test_sel.in == s.in &&
                                        test_sel.out == s.out) {
                                    found = true;
                                    break;
                                }
                            }
                            if (!found) {
                                Selection link_sel = {s.in, s.out, link->track};
                                panel_timeline->selections.append(link_sel);
                            }
                        }
                    }
                }
            }
        }

        if (panel_timeline->edit_tool_also_seeks) {
            panel_timeline->seek(qMin(panel_timeline->drag_frame_start, panel_timeline->cursor_frame));
        } else {
            panel_timeline->repaint_timeline();
        }
    } else if (panel_timeline->moving_init) {
        if (track_resizing) {
            int diff = track_resize_mouse_cache - event->pos().y();
            int new_height = track_resize_old_value;
            if (bottom_align) {
                new_height += diff;
            } else {
                new_height -= diff;
            }
            if (new_height < TRACK_MIN_HEIGHT) new_height = TRACK_MIN_HEIGHT;
            panel_timeline->calculate_track_height(track_target, new_height);
            redraw_clips();
        } else if (panel_timeline->moving_proc) {
            QPoint pos = event->pos();
            update_ghosts(pos);
		} else {
			// set up movement
			// create ghosts
            for (int i=0;i<sequence->clip_count();i++) {
                Clip* c = sequence->get_clip(i);
                if (c != NULL && panel_timeline->is_clip_selected(c)) {
                    Ghost g;
                    g.clip = i;
                    g.in = c->timeline_in;
                    g.out = c->timeline_out;
                    g.track = c->track;
                    g.clip_in = c->clip_in;
                    panel_timeline->ghosts.append(g);
				}
			}

			// ripple edit prep
			if (panel_timeline->tool == TIMELINE_TOOL_RIPPLE) {
                QVector<Clip*> axis_ghosts;
                for (int i=0;i<panel_timeline->ghosts.size();i++) {
                    Clip* c = sequence->get_clip(panel_timeline->ghosts.at(i).clip);
                    bool found = false;
                    for (int j=0;j<axis_ghosts.size();j++) {
                        Clip* cc = axis_ghosts.at(j);
                        if (c->track == cc->track &&
                                ((panel_timeline->trim_in && c->timeline_in < cc->timeline_in) ||
                                (!panel_timeline->trim_in && c->timeline_out > cc->timeline_out))) {
                            axis_ghosts[j] = c;
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        axis_ghosts.append(c);
                    }
                }

                for (int i=0;i<sequence->clip_count();i++) {
                    Clip* c = sequence->get_clip(i);
                    if (c != NULL && !panel_timeline->is_clip_selected(c)) {
                        for (int j=0;j<axis_ghosts.size();j++) {
                            Clip* axis = axis_ghosts.at(j);
                            long comp_point = (panel_timeline->trim_in) ? axis->timeline_in : axis->timeline_out;
                            bool clip_is_pre = (c->timeline_in < comp_point);
                            QVector<Clip*>& clip_list = clip_is_pre ? pre_clips : post_clips;
                            bool found = false;
                            for (int k=0;k<clip_list.size();k++) {
                                Clip* cached = clip_list.at(k);
                                if (cached->track == c->track) {
                                    if (clip_is_pre == (c->timeline_in > cached->timeline_in)) {
                                        clip_list[k] = c;
                                    }
                                    found = true;
                                    break;
                                }
                            }
                            if (!found) {
                                clip_list.append(c);
                            }
                        }
                    }
                }
//                qDebug() << "found" << pre_clips.size() << "pres and" << post_clips.size() << "posts";
			}

			init_ghosts();

			panel_timeline->moving_proc = true;
		}
		panel_timeline->repaint_timeline();
    } else if (panel_timeline->splitting) {
        int track = panel_timeline->cursor_track;
		bool repaint = false;
        for (int i=0;i<sequence->clip_count();i++) {
            Clip* clip = sequence->get_clip(i);
            if (clip != NULL && clip->track == track) {
                panel_timeline->split_clip_and_relink(i, panel_timeline->drag_frame_start, !alt);
				repaint = true;
			}
		}

		// redraw clips since we changed them
        if (repaint) panel_timeline->redraw_all_clips(true);
    } else if (panel_timeline->rect_select_init) {
        if (panel_timeline->rect_select_proc) {
            panel_timeline->rect_select_w = event->pos().x() - panel_timeline->rect_select_x;
            panel_timeline->rect_select_h = event->pos().y() - panel_timeline->rect_select_y;
            if (bottom_align) panel_timeline->rect_select_h -= height();

            long frame_start = panel_timeline->getFrameFromScreenPoint(panel_timeline->rect_select_x);
            long frame_end = panel_timeline->getFrameFromScreenPoint(event->pos().x());
            long frame_min = qMin(frame_start, frame_end);
            long frame_max = qMax(frame_start, frame_end);

            int rsy = panel_timeline->rect_select_y;
            if (bottom_align) rsy += height();
            int track_start = getTrackFromScreenPoint(rsy);
            int track_end = getTrackFromScreenPoint(event->pos().y());
            int track_min = qMin(track_start, track_end);
            int track_max = qMax(track_start, track_end);

            QVector<Clip*> selected_clips;
            for (int i=0;i<sequence->clip_count();i++) {
                Clip* clip = sequence->get_clip(i);
                if (clip != NULL &&
                        clip->track >= track_min &&
                        clip->track <= track_max &&
                        !(clip->timeline_in < frame_min && clip->timeline_out < frame_min) &&
                        !(clip->timeline_in > frame_max && clip->timeline_out > frame_max)) {
                    QVector<Clip*> session_clips;
                    session_clips.append(clip);

                    if (!alt) {
                        for (int j=0;j<clip->linked.size();j++) {
                            session_clips.append(sequence->get_clip(clip->linked.at(j)));
                        }
                    }

                    for (int j=0;j<session_clips.size();j++) {
                        // see if clip has already been added - this can easily happen due to adding linked clips
                        bool found = false;
                        Clip* c = session_clips.at(j);
                        for (int k=0;k<selected_clips.size();k++) {
                            if (selected_clips.at(k) == c) {
                                found = true;
                                break;
                            }
                        }
                        if (!found) {
                            selected_clips.append(c);
                        }
                    }
                }
            }

            panel_timeline->selections.resize(selected_clips.size() + panel_timeline->selection_offset);
            for (int i=0;i<selected_clips.size();i++) {
                Selection& s = panel_timeline->selections[i+panel_timeline->selection_offset];
                Clip* clip = selected_clips.at(i);
                s.old_in = s.in = clip->timeline_in;
                s.old_out = s.out = clip->timeline_out;
                s.old_track = s.track = clip->track;
            }

            panel_timeline->repaint_timeline();
        } else {
            panel_timeline->rect_select_x = event->pos().x();
            panel_timeline->rect_select_y = event->pos().y();
            if (bottom_align) panel_timeline->rect_select_y -= height();
            panel_timeline->rect_select_w = 0;
            panel_timeline->rect_select_h = 0;
            panel_timeline->rect_select_proc = true;
        }
	} else if (panel_timeline->tool == TIMELINE_TOOL_POINTER || panel_timeline->tool == TIMELINE_TOOL_RIPPLE) {
        track_resizing = false;

		QPoint pos = event->pos();

		int lim = 5;
        int mouse_track = getTrackFromScreenPoint(pos.y());
        long mouse_frame_lower = panel_timeline->getFrameFromScreenPoint(pos.x()-lim)-1;
        long mouse_frame_upper = panel_timeline->getFrameFromScreenPoint(pos.x()+lim)+1;
        bool found = false;
        int closeness = INT_MAX;
        for (int i=0;i<sequence->clip_count();i++) {
            Clip* c = sequence->get_clip(i);
            if (c != NULL && c->track == mouse_track) {
                if (c->timeline_in > mouse_frame_lower && c->timeline_in < mouse_frame_upper) {
                    int nc = qAbs(c->timeline_in + 1 - panel_timeline->cursor_frame);
                    if (nc < closeness) {
                        panel_timeline->trim_target = i;
                        panel_timeline->trim_in = true;
                        closeness = nc;
                        found = true;
                    }
                }
                if (c->timeline_out > mouse_frame_lower && c->timeline_out < mouse_frame_upper) {
                    int nc = qAbs(c->timeline_out - 1 - panel_timeline->cursor_frame);
                    if (nc < closeness) {
                        panel_timeline->trim_target = i;
                        panel_timeline->trim_in = false;
                        closeness = nc;
                        found = true;
                    }
				}
			}
        }
		if (found) {
			setCursor(Qt::SizeHorCursor);
		} else {
            panel_timeline->trim_target = -1;

            // look for track heights
            found = false;
            int track_y = 0;
            for (int i=0;i<panel_timeline->get_track_height_size(bottom_align);i++) {
                int track = (bottom_align) ? -1-i : i;
                int track_height = panel_timeline->calculate_track_height(track, -1);
                track_y += track_height;
                int y_test_value = (bottom_align) ? rect().bottom() - track_y : track_y;
                int test_range = 5;
                if (pos.y() > y_test_value-test_range && pos.y() < y_test_value+test_range) {
                    found = true;
                    track_resizing = true;
                    track_target = track;
                    track_resize_old_value = track_height;
                }
            }

            if (found) {
                setCursor(Qt::SizeVerCursor);
            } else {
                unsetCursor();
            }
		}
	} else if (panel_timeline->tool == TIMELINE_TOOL_EDIT || panel_timeline->tool == TIMELINE_TOOL_RAZOR) {
		// redraw because we have a cursor
		panel_timeline->repaint_timeline();
    } else if (panel_timeline->tool == TIMELINE_TOOL_SLIP) {
        if (getClipIndexFromCoords(panel_timeline->cursor_frame, panel_timeline->cursor_track) > -1) {
            setCursor(Qt::SizeHorCursor);
        } else {
            unsetCursor();
        }
    }
}

int color_brightness(int r, int g, int b) {
	return (0.2126*r + 0.7152*g + 0.0722*b);
}

void TimelineWidget::redraw_clips() {
    // Draw clips
    int panel_width = panel_timeline->getScreenPointFromFrame(sequence->getEndFrame()) + 100;

    if (minimumWidth() != panel_width || clip_pixmap.height() != height()) {
        setMinimumWidth(panel_width);
        clip_pixmap = QPixmap(panel_width, height());
    }

    clip_pixmap.fill(Qt::transparent);
    QPainter clip_painter(&clip_pixmap);
	int video_track_limit = 0;
    int audio_track_limit = 0;
    QColor transition_color(255, 0, 0, 16);
    for (int i=0;i<sequence->clip_count();i++) {
        Clip* clip = sequence->get_clip(i);
        if (clip != NULL && is_track_visible(clip->track)) {
            if (clip->track < 0 && clip->track < video_track_limit) { // video clip
                video_track_limit = clip->track;
            } else if (clip->track > audio_track_limit) {
                audio_track_limit = clip->track;
            }

            QRect clip_rect(panel_timeline->getScreenPointFromFrame(clip->timeline_in), getScreenPointFromTrack(clip->track), clip->getLength() * panel_timeline->zoom, panel_timeline->calculate_track_height(clip->track, -1));
            clip_painter.fillRect(clip_rect, QColor(clip->color_r, clip->color_g, clip->color_b));

            int thumb_x = clip_rect.x() + 1;

            QRect text_rect(clip_rect.left() + CLIP_TEXT_PADDING, clip_rect.top() + CLIP_TEXT_PADDING, clip_rect.width() - CLIP_TEXT_PADDING - 1, clip_rect.height() - CLIP_TEXT_PADDING - 1);

            // draw clip transitions
            for (char i=0;i<2;i++) {
                Transition* t;
                if (i == 0) {
                    t = clip->opening_transition;
                } else {
                    t = clip->closing_transition;
                }
                if (t != NULL) {
                    int transition_width = panel_timeline->getScreenPointFromFrame(t->length);
                    int transition_height = clip_rect.height() * 0.6;
                    if (transition_height <= TRACK_MIN_HEIGHT) {
                        transition_height = clip_rect.height();
                    }
                    int tr_y = clip_rect.y() + ((clip_rect.height()-transition_height)/2);
                    int tr_x = 0;
                    if (i == 0) {
                        tr_x = clip_rect.x();
                        text_rect.setX(text_rect.x()+transition_width);
                        thumb_x += transition_width;
                    } else {
                        tr_x = clip_rect.right()-transition_width;
                        text_rect.setWidth(text_rect.width()-transition_width);
                    }
                    QRect transition_rect = QRect(tr_x, tr_y, transition_width, transition_height);
                    clip_painter.fillRect(transition_rect, transition_color);
                    QRect transition_text_rect(transition_rect.x() + CLIP_TEXT_PADDING, transition_rect.y() + CLIP_TEXT_PADDING, transition_rect.width() - CLIP_TEXT_PADDING, transition_rect.height() - CLIP_TEXT_PADDING);
                    if (transition_text_rect.width() > MAX_TEXT_WIDTH) {
                        clip_painter.setPen(QColor(0, 0, 0, 96));
                        if (i == 0) {
                            clip_painter.drawLine(transition_rect.bottomLeft(), transition_rect.topRight());
                        } else {
                            clip_painter.drawLine(transition_rect.topLeft(), transition_rect.bottomRight());
                        }

                        clip_painter.setPen(Qt::white);
                        clip_painter.drawText(transition_text_rect, 0, t->name, &transition_text_rect);
                    }
                    clip_painter.setPen(Qt::black);
                    clip_painter.drawRect(transition_rect);
                }
            }

            // draw thumbnail/waveform
            if (clip->media_stream->preview_done) {
                if (clip->track < 0) {
                    int thumb_y = clip_painter.fontMetrics().height()+CLIP_TEXT_PADDING+CLIP_TEXT_PADDING;
                    int thumb_height = clip_rect.height()-thumb_y;
                    int thumb_width = (thumb_height*((float)clip->media_stream->video_preview.width()/(float)clip->media_stream->video_preview.height()));
                    if (thumb_height > thumb_y && text_rect.width() + CLIP_TEXT_PADDING > thumb_width) { // at small clip heights, don't even draw it
                        QRect thumb_rect(thumb_x, clip_rect.y()+thumb_y, thumb_width, thumb_height);
                        clip_painter.drawImage(thumb_rect, clip->media_stream->video_preview);
                    }
                } else if (clip_rect.height() > TRACK_MIN_HEIGHT) {
                    int divider = clip->media_stream->audio_channels*2;

                    clip_painter.setPen(QColor(80, 80, 80));
                    int channel_height = clip_rect.height()/clip->media_stream->audio_channels;
                    long media_length = clip->media->get_length_in_frames(clip->sequence->frame_rate);
                    for (int i=0;i<clip_rect.width();i++) {
                        int waveform_index = qFloor((((clip->clip_in + ((float) i/panel_timeline->zoom))/media_length) * clip->media_stream->audio_preview.size())/divider)*divider;

                        for (int j=0;j<clip->media_stream->audio_channels;j++) {
                            int mid = clip_rect.top()+channel_height*j+(channel_height/2);
                            int offset = waveform_index+(j*2);
                            qint8 min = (float)clip->media_stream->audio_preview[offset] / 128.0f * (channel_height/2);
                            qint8 max = (float)clip->media_stream->audio_preview[offset+1] / 128.0f * (channel_height/2);
                            clip_painter.drawLine(clip_rect.left()+i, mid+min, clip_rect.left()+i, mid+max);
                        }
                    }
                }
            }

            // top left bevel
            clip_painter.setPen(Qt::white);
            clip_painter.drawLine(clip_rect.bottomLeft(), clip_rect.topLeft());
            clip_painter.drawLine(clip_rect.topLeft(), clip_rect.topRight());

            // draw text
            if (text_rect.width() > MAX_TEXT_WIDTH) {
                if (color_brightness(clip->color_r, clip->color_g, clip->color_b) > 160) {
                    // set to black if color is bright
                    clip_painter.setPen(Qt::black);
                }
                if (clip->linked.size() > 0) {
                    int underline_y = CLIP_TEXT_PADDING + clip_painter.fontMetrics().height() + clip_rect.top();
                    int underline_width = qMin(text_rect.width() - 1, clip_painter.fontMetrics().width(clip->name));
                    clip_painter.drawLine(text_rect.x(), underline_y, text_rect.x() + underline_width, underline_y);
                }
                clip_painter.drawText(text_rect, 0, clip->name, &text_rect);

                // bottom right gray
                clip_painter.setPen(Qt::gray);
                clip_painter.drawLine(clip_rect.bottomLeft(), clip_rect.bottomRight());
                clip_painter.drawLine(clip_rect.bottomRight(), clip_rect.topRight());
            }
        }
    }

	// Draw track lines
	if (show_track_lines) {
		clip_painter.setPen(QColor(0, 0, 0, 96));
		audio_track_limit++;
		if (video_track_limit == 0) video_track_limit--;

		if (bottom_align) {
			// only draw lines for video tracks
			for (int i=video_track_limit;i<0;i++) {
				int line_y = getScreenPointFromTrack(i) - 1;
				clip_painter.drawLine(0, line_y, rect().width(), line_y);
			}
		} else {
			// only draw lines for audio tracks
			for (int i=0;i<audio_track_limit;i++) {
                // TODO just make i+1?
                int line_y = getScreenPointFromTrack(i) + panel_timeline->calculate_track_height(i, -1);
				clip_painter.drawLine(0, line_y, rect().width(), line_y);
			}
		}
	}

	update();
}

void TimelineWidget::paintEvent(QPaintEvent*) {
    if (sequence != NULL) {
		QPainter p(this);

        p.drawPixmap(0, 0, minimumWidth(), height(), clip_pixmap);

		// Draw selections
		for (int i=0;i<panel_timeline->selections.size();i++) {
			const Selection& s = panel_timeline->selections.at(i);
			if (is_track_visible(s.track)) {
				int selection_y = getScreenPointFromTrack(s.track);
                int selection_x = panel_timeline->getScreenPointFromFrame(s.in);
                p.fillRect(selection_x, selection_y, panel_timeline->getScreenPointFromFrame(s.out) - selection_x, panel_timeline->calculate_track_height(s.track, -1), QColor(0, 0, 0, 64));
			}
		}

        // draw rectangle select
        if (panel_timeline->rect_select_proc) {
            int rsy = panel_timeline->rect_select_y;
            int rsh = panel_timeline->rect_select_h;
            if (bottom_align) {
                rsy += height();
            }
            QRect rect_select(panel_timeline->rect_select_x, rsy, panel_timeline->rect_select_w, rsh);
            p.setPen(QColor(204, 204, 204));
            p.drawRect(rect_select);
            p.fillRect(rect_select, QColor(0, 0, 0, 32));
        }

		// Draw ghosts
		for (int i=0;i<panel_timeline->ghosts.size();i++) {
			const Ghost& g = panel_timeline->ghosts.at(i);
			if (is_track_visible(g.track)) {
                int ghost_x = panel_timeline->getScreenPointFromFrame(g.in);
				int ghost_y = getScreenPointFromTrack(g.track);
                int ghost_width = panel_timeline->getScreenPointFromFrame(g.out - g.in) - 1;
                int ghost_height = panel_timeline->calculate_track_height(g.track, -1) - 1;
				p.setPen(QColor(255, 255, 0));
				for (int j=0;j<GHOST_THICKNESS;j++) {
					p.drawRect(ghost_x+j, ghost_y+j, ghost_width-j-j, ghost_height-j-j);
				}
			}
		}

        // Draw playhead
        p.setPen(Qt::red);
        int playhead_x = panel_timeline->getScreenPointFromFrame(panel_timeline->playhead);
        p.drawLine(playhead_x, rect().top(), playhead_x, rect().bottom());

        p.setPen(QColor(0, 0, 0, 64));
        int edge_y = (bottom_align) ? rect().height()-1 : 0;
        p.drawLine(0, edge_y, rect().width(), edge_y);

        // draw snap point
        if (panel_timeline->snapped) {
            p.setPen(Qt::white);
            int snap_x = panel_timeline->getScreenPointFromFrame(panel_timeline->snap_point);
            p.drawLine(snap_x, 0, snap_x, height());
        }

		// Draw edit cursor
		if (panel_timeline->tool == TIMELINE_TOOL_EDIT || panel_timeline->tool == TIMELINE_TOOL_RAZOR) {
            if (is_track_visible(panel_timeline->cursor_track)) {
                int cursor_x = panel_timeline->getScreenPointFromFrame(panel_timeline->cursor_frame);
                int cursor_y = getScreenPointFromTrack(panel_timeline->cursor_track);

				p.setPen(Qt::gray);
                p.drawLine(cursor_x, cursor_y, cursor_x, cursor_y + panel_timeline->calculate_track_height(panel_timeline->cursor_track, -1));
			}
        }
	}
}

bool TimelineWidget::is_track_visible(int track) {
    return (bottom_align == (track < 0));
}

// **************************************
// screen point <-> frame/track functions
// **************************************

int TimelineWidget::getTrackFromScreenPoint(int y) {
    if (bottom_align) {
        y = -(y - rect().height());
    }
    y--;
    int height_measure = 0;
    int counter = ((!bottom_align && y > 0) || (bottom_align && y < 0)) ? 0 : -1;
    int track_height = panel_timeline->calculate_track_height(counter, -1);
    while (qAbs(y) > height_measure+track_height) {
        if (show_track_lines && counter != -1) y--;
        height_measure += track_height;
        if ((!bottom_align && y > 0) || (bottom_align && y < 0)) {
            counter++;
        } else {
            counter--;
        }
        track_height = panel_timeline->calculate_track_height(counter, -1);
    }
    return counter;
}

int TimelineWidget::getScreenPointFromTrack(int track) {
    int y = 0;
    int counter = 0;
    while (counter != track) {
        if (bottom_align) counter--;
        y += panel_timeline->calculate_track_height(counter, -1);
        if (!bottom_align) counter++;
        if (show_track_lines && counter != -1) y++;
    }
    y++;
    return (bottom_align) ? rect().height() - y : y;
}

int TimelineWidget::getClipIndexFromCoords(long frame, int track) {
    for (int i=0;i<sequence->clip_count();i++) {
        Clip* c = sequence->get_clip(i);
        if (c != NULL && c->track == track && frame >= c->timeline_in && frame < c->timeline_out) {
            return i;
		}
	}
	return -1;
}