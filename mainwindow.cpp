﻿#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "io/config.h"

#include "project/sequence.h"

#include "panels/panels.h"
#include "panels/project.h"
#include "panels/effectcontrols.h"
#include "panels/viewer.h"
#include "panels/timeline.h"

#include "dialogs/aboutdialog.h"
#include "dialogs/newsequencedialog.h"
#include "dialogs/exportdialog.h"
#include "dialogs/preferencesdialog.h"

#include "ui_timeline.h"

#include <QDebug>
#include <QStyleFactory>
#include <QMessageBox>
#include <QFileDialog>
#include <QStandardPaths>
#include <QTimer>

#define OLIVE_FILE_FILTER "Olive Project (*.ove)"

QString autorecovery_filename;
QTimer autorecovery_timer;

void MainWindow::setup_layout() {
    panel_project->show();
    panel_effect_controls->show();
    panel_viewer->show();
    panel_timeline->show();

    addDockWidget(Qt::TopDockWidgetArea, panel_project);
    addDockWidget(Qt::TopDockWidgetArea, panel_effect_controls);
    addDockWidget(Qt::TopDockWidgetArea, panel_viewer);
    addDockWidget(Qt::BottomDockWidgetArea, panel_timeline);
}

MainWindow::MainWindow(QWidget *parent) :
	QMainWindow(parent),
	ui(new Ui::MainWindow)
{
	// set up style?

    qApp->setStyle(QStyleFactory::create("Fusion"));

	QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, QColor(53,53,53));
	darkPalette.setColor(QPalette::WindowText, Qt::white);
	darkPalette.setColor(QPalette::Base, QColor(25,25,25));
	darkPalette.setColor(QPalette::AlternateBase, QColor(53,53,53));
	darkPalette.setColor(QPalette::ToolTipBase, Qt::white);
	darkPalette.setColor(QPalette::ToolTipText, Qt::white);
	darkPalette.setColor(QPalette::Text, Qt::white);
	darkPalette.setColor(QPalette::Button, QColor(53,53,53));
	darkPalette.setColor(QPalette::ButtonText, Qt::white);	darkPalette.setColor(QPalette::BrightText, Qt::red);
	darkPalette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(128, 128, 128));
	darkPalette.setColor(QPalette::Link, QColor(42, 130, 218));
	darkPalette.setColor(QPalette::Highlight, QColor(42, 130, 218));
	darkPalette.setColor(QPalette::HighlightedText, Qt::black);

    qApp->setPalette(darkPalette);

	// end style

	setWindowState(Qt::WindowMaximized);

	ui->setupUi(this);

    setWindowTitle("Olive (July 2018 | Pre-Alpha)");
    statusBar()->showMessage("Welcome to Olive");

    ui->centralWidget->setMaximumSize(0, 0);
    setDockNestingEnabled(true);

    // TODO maybe replace these with non-pointers later on?
    panel_project = new Project(this);
    panel_effect_controls = new EffectControls(this);
    panel_viewer = new Viewer(this);
    panel_timeline = new Timeline(this);

	setup_layout();

    connect(ui->menuWindow, SIGNAL(aboutToShow()), this, SLOT(windowMenu_About_To_Be_Shown()));
    connect(ui->menu_View, SIGNAL(aboutToShow()), this, SLOT(viewMenu_About_To_Be_Shown()));
    connect(ui->menu_Tools, SIGNAL(aboutToShow()), this, SLOT(toolMenu_About_To_Be_Shown()));

    QString data_dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!data_dir.isEmpty()) {
        QDir dir(data_dir);
        dir.mkpath(".");
        if (dir.exists()) {
            autorecovery_filename = data_dir + "/autorecovery.ove";
            if (QFile::exists(autorecovery_filename)) {
                if (QMessageBox::question(NULL, "Auto-recovery", "Olive didn't close properly and an autorecovery file was detected. Would you like to open it?", QMessageBox::Yes, QMessageBox::No) == QMessageBox::Yes) {
                    project_url = autorecovery_filename;
                    panel_project->load_project();
                }
            }
            autorecovery_timer.setInterval(60000);
            QObject::connect(&autorecovery_timer, SIGNAL(timeout()), this, SLOT(autorecover_interval()));
            autorecovery_timer.start();
        }
    }
}

MainWindow::~MainWindow() {
    QString data_dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!data_dir.isEmpty() && !autorecovery_filename.isEmpty()) {
        if (QFile::exists(autorecovery_filename)) {
            QFile::remove(autorecovery_filename);
        }
    }

	delete ui;

    delete panel_project;
    delete panel_effect_controls;
    delete panel_viewer;
    delete panel_timeline;
}

void MainWindow::on_action_Import_triggered()
{
	panel_project->import_dialog();
}

void MainWindow::on_actionExit_triggered()
{
	QApplication::quit();
}

void MainWindow::on_actionAbout_triggered()
{
	AboutDialog a(this);
	a.exec();
}

void MainWindow::on_actionDelete_triggered()
{
	if (panel_timeline->focused()) {
		panel_timeline->delete_selection(false);
    } else if (panel_effect_controls->is_focused()) {
        panel_effect_controls->delete_clips();
    } else if (panel_project->is_focused()) {
        panel_project->delete_selected_media();
    }
}

void MainWindow::on_actionSelect_All_triggered()
{
	if (panel_timeline->focused()) {
		panel_timeline->select_all();
	}
}

void MainWindow::on_actionSequence_triggered()
{
    NewSequenceDialog nsd(this);
	nsd.set_sequence_name(panel_project->get_next_sequence_name());
	nsd.exec();
}

void MainWindow::on_actionZoom_In_triggered()
{
	if (panel_timeline->focused()) {
        panel_timeline->set_zoom(true);
	}
}

void MainWindow::on_actionZoom_out_triggered()
{
	if (panel_timeline->focused()) {
        panel_timeline->set_zoom(false);
	}
}

void MainWindow::on_actionTimeline_Track_Lines_toggled(bool e)
{
	show_track_lines = e;
    panel_timeline->redraw_all_clips(false);
}

void MainWindow::on_actionExport_triggered()
{
    ExportDialog e(this);
	e.exec();
}

void MainWindow::on_actionProject_2_toggled(bool arg1)
{
	panel_project->setVisible(arg1);
}

void MainWindow::on_actionEffect_Controls_toggled(bool arg1)
{
	panel_effect_controls->setVisible(arg1);
}

void MainWindow::on_actionViewer_toggled(bool arg1)
{
	panel_viewer->setVisible(arg1);
}

void MainWindow::on_actionTimeline_toggled(bool arg1)
{
	panel_timeline->setVisible(arg1);
}

void MainWindow::on_actionRipple_Delete_triggered()
{
	panel_timeline->delete_selection(true);
}

void MainWindow::on_action_Undo_triggered()
{
    if (panel_timeline->focused()) {
        panel_timeline->undo();
    }
}

void MainWindow::on_action_Redo_triggered()
{
    if (panel_timeline->focused()) {
        panel_timeline->redo();
    }
}

void MainWindow::on_actionSplit_at_Playhead_triggered()
{
    if (panel_timeline->focused()) {
        panel_timeline->split_at_playhead();
    }
}

void MainWindow::on_actionCu_t_triggered()
{
    if (panel_timeline->focused()) {
        panel_timeline->copy(true);
    }
}

void MainWindow::on_actionCop_y_triggered()
{
    if (panel_timeline->focused()) {
        panel_timeline->copy(false);
    }
}

void MainWindow::on_action_Paste_triggered()
{
    if (panel_timeline->focused()) {
        panel_timeline->paste();
    }
}

void MainWindow::autorecover_interval() {
    bool old_changed = project_changed;
    QString old_filename = project_url;
    project_url = autorecovery_filename;
    qDebug() << "[INFO] Auto-recovery project saved";
    panel_project->save_project();
    project_url = old_filename;
    project_changed = old_changed;
}

bool MainWindow::save_project_as() {
    QString fn = QFileDialog::getSaveFileName(this, "Save Project As...", "", OLIVE_FILE_FILTER);
    if (!fn.isEmpty()) {
        if (!fn.endsWith(".ove", Qt::CaseInsensitive)) {
            fn += ".ove";
        }
        project_url = fn;
        panel_project->save_project();
        return true;
    }
    return false;
}

bool MainWindow::save_project() {
    if (project_url.isEmpty()) {
        return save_project_as();
    } else {
        panel_project->save_project();
        return true;
    }
}

bool MainWindow::can_close_project() {
    if (project_changed) {
        int r = QMessageBox::question(this, "Unsaved Project", "This project has changed since it was last saved. Would you like to save it before closing?", QMessageBox::Yes|QMessageBox::No|QMessageBox::Cancel, QMessageBox::Yes);
        if (r == QMessageBox::Yes) {
            return save_project();
        } else if (r == QMessageBox::Cancel) {
            return false;
        }
    }
    return true;
}

void MainWindow::on_action_Save_Project_triggered()
{
    save_project();
}

void MainWindow::on_action_Open_Project_triggered()
{
    QString fn = QFileDialog::getOpenFileName(this, "Open Project...", "", OLIVE_FILE_FILTER);
    if (!fn.isEmpty() && can_close_project()) {
        project_url = fn;
        panel_project->load_project();
    }
}

void MainWindow::on_actionProject_triggered()
{
    if (can_close_project()) {
        panel_project->new_project();
    }
}

void MainWindow::on_actionSave_Project_As_triggered()
{
    save_project_as();
}

void MainWindow::on_actionDeselect_All_triggered()
{
    if (panel_timeline->focused()) {
        panel_timeline->deselect();
    }
}

void MainWindow::on_actionGo_to_start_triggered()
{
    if (panel_timeline->focused() || panel_viewer->hasFocus()) {
        panel_timeline->go_to_start();
    }
}

void MainWindow::on_actionReset_to_default_layout_triggered()
{
    setup_layout();
}

void MainWindow::on_actionPrevious_Frame_triggered()
{
    if (panel_timeline->focused() || panel_viewer->hasFocus()) {
        panel_timeline->previous_frame();
    }
}

void MainWindow::on_actionNext_Frame_triggered()
{
    if (panel_timeline->focused() || panel_viewer->hasFocus()) {
        panel_timeline->next_frame();
    }
}

void MainWindow::on_actionGo_to_End_triggered()
{
    if (panel_timeline->focused() || panel_viewer->hasFocus()) {
        panel_timeline->go_to_end();
    }
}

void MainWindow::on_actionPlay_Pause_triggered()
{
    if (panel_timeline->focused() || panel_viewer->hasFocus()) {
        panel_timeline->toggle_play();
    }
}

void MainWindow::on_actionCrash_triggered()
{
    if (QMessageBox::warning(this, "Are you sure you want to crash?", "WARNING: This is a debugging function designed to crash the program. Olive WILL crash and any unsaved progress WILL be lost. Are you sure you wish to do this?", QMessageBox::Yes, QMessageBox::No) == QMessageBox::Yes) {
        // intentionally tries to crash the program - mostly used for debugging
        Timeline* temp = NULL;
        temp->snapped = true;
    }
}

void MainWindow::on_actionEdit_Tool_triggered()
{
    if (panel_timeline->focused()) panel_timeline->ui->toolEditButton->click();
}

void MainWindow::on_actionToggle_Snapping_triggered()
{
    if (panel_timeline->focused()) panel_timeline->ui->snappingButton->click();
}

void MainWindow::on_actionPointer_Tool_triggered()
{
    if (panel_timeline->focused()) panel_timeline->ui->toolArrowButton->click();
}

void MainWindow::on_actionRazor_Tool_triggered()
{
    if (panel_timeline->focused()) panel_timeline->ui->toolRazorButton->click();
}

void MainWindow::on_actionRipple_Tool_triggered()
{
    if (panel_timeline->focused()) panel_timeline->ui->toolRippleButton->click();
}

void MainWindow::on_actionRolling_Tool_triggered()
{
    if (panel_timeline->focused()) panel_timeline->ui->toolRollingButton->click();
}

void MainWindow::on_actionSlip_Tool_triggered()
{
    if (panel_timeline->focused()) panel_timeline->ui->toolSlipButton->click();
}

void MainWindow::on_actionGo_to_Previous_Cut_triggered()
{
    if (panel_timeline->focused() || panel_viewer->hasFocus()) {
        panel_timeline->previous_cut();
    }
}

void MainWindow::on_actionGo_to_Next_Cut_triggered()
{
    if (panel_timeline->focused() || panel_viewer->hasFocus()) {
        panel_timeline->next_cut();
    }
}

void MainWindow::on_actionPreferences_triggered()
{
    PreferencesDialog pd(this);
    pd.exec();
}

void MainWindow::on_actionIncrease_Track_Height_triggered()
{
    panel_timeline->increase_track_height();
}

void MainWindow::on_actionDecrease_Track_Height_triggered()
{
    panel_timeline->decrease_track_height();
}

void MainWindow::windowMenu_About_To_Be_Shown() {
    ui->actionProject_2->setChecked(panel_project->isVisible());
    ui->actionEffect_Controls->setChecked(panel_effect_controls->isVisible());
    ui->actionTimeline->setChecked(panel_timeline->isVisible());
    ui->actionViewer->setChecked(panel_viewer->isVisible());
}

void MainWindow::viewMenu_About_To_Be_Shown() {
    ui->actionFrames->setChecked(panel_viewer->timecode_view == TIMECODE_FRAMES);
    ui->actionDrop_Frame->setChecked(panel_viewer->timecode_view == TIMECODE_DROP);
    if (sequence != NULL) ui->actionDrop_Frame->setEnabled(frame_rate_is_droppable(sequence->frame_rate));
    ui->actionNon_Drop_Frame->setChecked(panel_viewer->timecode_view == TIMECODE_NONDROP);
}

void MainWindow::on_actionFrames_triggered()
{
    panel_viewer->timecode_view = TIMECODE_FRAMES;
    if (sequence != NULL) {
        panel_viewer->update_playhead_timecode();
        panel_viewer->update_end_timecode();
    }
}

void MainWindow::on_actionDrop_Frame_triggered()
{
    panel_viewer->timecode_view = TIMECODE_DROP;
    if (sequence != NULL) {
        panel_viewer->update_playhead_timecode();
        panel_viewer->update_end_timecode();
    }
}

void MainWindow::on_actionNon_Drop_Frame_triggered()
{
    panel_viewer->timecode_view = TIMECODE_NONDROP;
    if (sequence != NULL) {
        panel_viewer->update_playhead_timecode();
        panel_viewer->update_end_timecode();
    }
}

void MainWindow::toolMenu_About_To_Be_Shown() {
    ui->actionEdit_Tool_Also_Seeks->setChecked(panel_timeline->edit_tool_also_seeks);
    ui->actionEdit_Tool_Selects_Links->setChecked(panel_timeline->edit_tool_selects_links);
    ui->actionSelecting_Also_Seeks->setChecked(panel_timeline->select_also_seeks);
    ui->actionSeek_to_the_End_of_Pastes->setChecked(panel_timeline->paste_seeks);
    ui->actionToggle_Snapping->setChecked(panel_timeline->snapping);
}

void MainWindow::on_actionEdit_Tool_Selects_Links_triggered() {
    panel_timeline->edit_tool_selects_links = !panel_timeline->edit_tool_selects_links;
}

void MainWindow::on_actionEdit_Tool_Also_Seeks_triggered() {
    panel_timeline->edit_tool_also_seeks = !panel_timeline->edit_tool_also_seeks;
}

void MainWindow::on_actionDuplicate_triggered() {
    if (panel_project->is_focused()) {
        panel_project->duplicate_selected();
    }
}

void MainWindow::on_actionSelecting_Also_Seeks_triggered() {
    panel_timeline->select_also_seeks = !panel_timeline->select_also_seeks;
}

void MainWindow::on_actionSeek_to_the_End_of_Pastes_triggered()
{
    panel_timeline->paste_seeks = !panel_timeline->paste_seeks;
}

void MainWindow::on_actionAdd_Default_Transition_triggered() {
    if (panel_timeline->focused()) panel_timeline->add_transition();
}