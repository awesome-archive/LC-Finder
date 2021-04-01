﻿/* ***************************************************************************
 * settings_detector.c -- detector setting view
 *
 * Copyright (C) 2019-2020 by Liu Chao <lc-soft@live.cn>
 *
 * This file is part of the LC-Finder project, and may only be used, modified,
 * and distributed under the terms of the GPLv2.
 *
 * By continuing to use, modify, or distribute this file you indicate that you
 * have read the license and understand and accept it fully.
 *
 * The LC-Finder project is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GPL v2 for more details.
 *
 * You should have received a copy of the GPLv2 along with this file. It is
 * usually in the LICENSE.TXT file, If not, see <http://www.gnu.org/licenses/>.
 * ****************************************************************************/

/* ****************************************************************************
 * settings_detector.c -- “设置”视图中的检测器设置项
 *
 * 版权所有 (C) 2019-2020 归属于 刘超 <lc-soft@live.cn>
 *
 * 这个文件是 LC-Finder 项目的一部分，并且只可以根据GPLv2许可协议来使用、更改和
 * 发布。
 *
 * 继续使用、修改或发布本文件，表明您已经阅读并完全理解和接受这个许可协议。
 *
 * LC-Finder 项目是基于使用目的而加以散布的，但不负任何担保责任，甚至没有适销
 * 性或特定用途的隐含担保，详情请参照GPLv2许可协议。
 *
 * 您应已收到附随于本文件的GPLv2许可协议的副本，它通常在 LICENSE 文件中，如果
 * 没有，请查看：<http://www.gnu.org/licenses/>.
 * ****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "finder.h"
#include <LCUI/timer.h>
#include <LCUI/gui/widget.h>
#include <LCUI/gui/widget/textview.h>
#include "ui.h"
#include "i18n.h"
#include "dialog.h"
#include "taskitem.h"
#include "textview_i18n.h"
#include "settings.h"
#include "detector.h"

// clang-format off

#define KEY_DIALOG_TITLE		"dialog.not_implement.title"
#define KEY_DIALOG_CONTENT		"dialog.not_implement.text"
#define KEY_MESSAGE_CREATING		"message.creating_model"
#define KEY_MESSAGE_REMOVING		"message.removing_model"
#define KEY_NEW_MODEL_TITLE		"dialog.new_model.title"
#define KEY_NEW_MODEL_PLACEHOLDER	"dialog.new_model.placeholder"
#define KEY_REMOVE_MODEL_TITLE		"dialog.remove_model.title"
#define KEY_CLASSES_TEXT		"settings.detector.model_classes"
#define KEY_DETECTOR_DETECTION_TITLE	"settings.detector.tasks.detection.title"
#define KEY_DETECTOR_DETECTION_TEXT	"settings.detector.tasks.detection.text"
#define KEY_DETECTOR_TRAINING_TITLE	"settings.detector.tasks.training.title"
#define KEY_DETECTOR_TRAINING_TEXT	"settings.detector.tasks.training.text"
#define KEY_DETECTOR_INIT_FAILED	"message.detector_init_failed"

typedef struct TaskControllerRec_ {
	int timer;
	LCUI_Widget view;
	DetectorTask task;
} TaskControllerRec, *TaskController;

static struct DetectorSettingView {
	LCUI_Widget view;
	LCUI_Widget list;
	LCUI_Widget dropdown;
	TaskControllerRec detection;
	TaskControllerRec training;
} view;

// clang-format on

static void RenderModels(void);

static void RenderModelClassesText(wchar_t *buff, const wchar_t *key,
				   void *data)
{
	DetectorModel model = data;

	swprintf(buff, TXTFMT_BUF_MAX_LEN, key, model->classes);
}

static void TaskForSetModel(void *arg1, void *arg2)
{
	wchar_t *name = arg1;
	wchar_t text[1024] = { 0 };
	const wchar_t *title, *message;

	if (Detector_SetModel(name) == 0) {
		wcscpy(finder.config.detector_model_name, name);
		LCFinder_SaveConfig();
	} else {
		message = DecodeANSI(Detector_GetLastErrorString());
		title = I18n_GetText(KEY_DETECTOR_INIT_FAILED);
		swprintf(text, 1023, L"%ls: %ls", title, message);
		TaskItem_SetError(view.detection.view, text);
		Detector_FreeTask(view.detection.task);
		view.detection.task = NULL;
	}
	free(name);
}

static void TaskForStartDetect(void *arg1, void *arg2)
{
	TaskController t = arg1;

	if (t->task) {
		Detector_RunTaskAync(t->task);
	}
}

static void TaskForStopDetect(void *arg1, void *arg2)
{
	DetectorTask task = arg1;
	LCUI_Widget view = arg2;

	Detector_CancelTask(task);
	Detector_FreeTask(task);
	TaskItem_SetActionDisabled(view, FALSE);
}

static void UITaskForUpdateModels(void *arg1, void *arg2)
{
	UI_StopLoading();
	RenderModels();
}

static void TaskForCreateModel(void *arg1, void *arg2)
{
	Detector_CreateModel(arg1);
	LCUI_PostSimpleTask(UITaskForUpdateModels, NULL, NULL);
}

static void TaskForRemoveModel(void *arg1, void *arg2)
{
	Detector_DestroyModel(arg1);
	LCUI_PostSimpleTask(UITaskForUpdateModels, NULL, NULL);
}

static void OnBtnRemoveModelClick(LCUI_Widget w, LCUI_WidgetEvent e, void *arg)
{
	LCUI_Widget window;
	LCUI_TaskRec task = { 0 };
	const wchar_t *title;

	title = I18n_GetText(KEY_REMOVE_MODEL_TITLE);
	window = LCUIWidget_GetById(ID_WINDOW_MAIN);
	if (LCUIDialog_Confirm(window, title, NULL)) {
		task.func = TaskForRemoveModel;
		task.arg[0] = e->data;
		UI_StartLoading(I18n_GetText(KEY_MESSAGE_REMOVING));
		LCUI_PostAsyncTask(&task);
	}
}

static LCUI_BOOL CheckModelName(const wchar_t *name)
{
	if (wgetcharcount(name, L"@ /\\$!&%")) {
		return FALSE;
	}
	return TRUE;
}

static void OnBtnNewModelClick(LCUI_Widget w, LCUI_WidgetEvent e, void *arg)
{
	LCUI_Widget window;
	LCUI_TaskRec task = { 0 };
	const wchar_t *placeholder;
	const wchar_t *title;
	wchar_t name[64];

	title = I18n_GetText(KEY_NEW_MODEL_TITLE);
	placeholder = I18n_GetText(KEY_NEW_MODEL_PLACEHOLDER);
	window = LCUIWidget_GetById(ID_WINDOW_MAIN);
	if (0 == LCUIDialog_Prompt(window, title, placeholder, NULL, name, 64,
				   CheckModelName)) {
		task.func = TaskForCreateModel;
		task.arg[0] = wcsdup2(name);
		task.destroy_arg[0] = free;
		UI_StartLoading(I18n_GetText(KEY_MESSAGE_CREATING));
		LCUI_PostAsyncTask(&task);
	}
}

static LCUI_Widget ModelItem_Create(DetectorModel model)
{
	LCUI_Widget item, classes, content, action, icon, text, btn;

	item = LCUIWidget_New(NULL);
	content = LCUIWidget_New(NULL);
	action = LCUIWidget_New(NULL);
	icon = LCUIWidget_New("icon");
	text = LCUIWidget_New("textview");
	btn = LCUIWidget_New("icon");
	classes = LCUIWidget_New("textview-i18n");
	Widget_AddClass(item, "item");
	Widget_AddClass(text, "item-text");
	Widget_AddClass(icon, "item-icon");
	Widget_AddClass(classes, "item-text text-muted");
	Widget_AddClass(content, "item-content");
	Widget_AddClass(action, "item-action");
	Widget_SetAttribute(btn, "name", "close");
	Widget_SetAttribute(icon, "name", "buffer");
	TextView_SetTextW(text, model->name);
	TextViewI18n_SetKey(classes, KEY_CLASSES_TEXT);
	TextViewI18n_SetFormater(classes, RenderModelClassesText, model);
	TextViewI18n_Refresh(classes);
	Widget_BindEvent(btn, "click", OnBtnRemoveModelClick, model, NULL);
	Widget_Append(action, btn);
	Widget_Append(content, text);
	Widget_Append(content, classes);
	Widget_Append(item, icon);
	Widget_Append(item, content);
	Widget_Append(item, action);
	return item;
}

static void RefreshDropdownText(void)
{
	LCUI_Widget txt;

	SelectWidget(txt, ID_TXT_CURRENT_DETECTOR_MODEL);
	TextView_SetTextW(txt, finder.config.detector_model_name);
}

static void OnChangeModel(LCUI_Widget w, LCUI_WidgetEvent e, void *arg)
{
	const wchar_t *value;

	value = (const wchar_t *)Widget_GetAttribute(e->target, "value");
	if (!value || wcslen(value) < 1) {
		return;
	}
	wcscpy(finder.config.detector_model_name, value);
	LCFinder_SaveConfig();
	RefreshDropdownText();
}

static void RenderModels(void)
{
	size_t i;
	wchar_t *selected_model;
	LCUI_Widget item;
	DetectorModel *models;
	LCUI_BOOL found_model = FALSE;

	Widget_Empty(view.list);
	Widget_Empty(view.dropdown);
	BindEvent(view.dropdown, "change.dropdown", OnChangeModel);

	Detector_GetModels(&models);
	selected_model = finder.config.detector_model_name;
	for (i = 0; models[i]; ++i) {
		item = LCUIWidget_New("textview");
		Widget_AddClass(item, "dropdown-item");
		Widget_SetAttributeEx(item, "value", models[i]->name, 0, NULL);
		Widget_Append(view.dropdown, item);
		TextView_SetTextW(item, models[i]->name);
		Widget_Append(view.list, ModelItem_Create(models[i]));
		if (wcscmp(selected_model, models[i]->name) == 0) {
			found_model = TRUE;
		}
	}
	if (i > 0) {
		if (!found_model) {
			wcscpy(finder.config.detector_model_name,
			       models[i - 1]->name);
			TaskItem_SetActionDisabled(view.detection.view, TRUE);
			LCFinder_SaveConfig();
		}
		Widget_AddClass(view.view, "models-available");
	} else {
		Widget_AddClass(view.view, "models-unavailable");
	}
	RefreshDropdownText();
	free(models);
}

static int SetDetectorModelAsync(const wchar_t *name)
{
	LCUI_TaskRec task = { 0 };

	task.arg[0] = wcsdup2(name);
	task.func = TaskForSetModel;
	return LCUI_PostAsyncTask(&task);
}

static void OnDetectionProgress(void *arg)
{
	TaskController t = &view.detection;

	if (!t->task) {
		return;
	}
	TaskItem_SetProgress(t->view, t->task->current, t->task->total);
	if (t->task->state == DETECTOR_TASK_STATE_FINISHED) {
		TaskItem_StopTask(t->view);
		Detector_FreeTask(t->task);
		t->task = NULL;
	}
}

static void OnStartTrain(LCUI_Widget w, LCUI_WidgetEvent e, void *arg)
{
	const wchar_t *title = I18n_GetText(KEY_DIALOG_TITLE);
	const wchar_t *text = I18n_GetText(KEY_DIALOG_CONTENT);
	LCUI_Widget window = LCUIWidget_GetById(ID_WINDOW_MAIN);

	LCUIDialog_Alert(window, title, text);
	TaskItem_StopTask(view.training.view);
}

static void OnStartDetect(LCUI_Widget w, LCUI_WidgetEvent e, void *arg)
{
	int worker;
	LCUI_TaskRec task = { 0 };
	TaskController t = &view.detection;
	wchar_t *model = finder.config.detector_model_name;

	t->task = Detector_CreateTask(DETECTOR_TASK_DETECT);
	t->timer = LCUI_SetInterval(500, OnDetectionProgress, NULL);
	task.arg[0] = t;
	task.func = TaskForStartDetect;
	worker = SetDetectorModelAsync(model);
	LCUI_PostAsyncTaskTo(&task, worker);
}

static void OnStopDetect(LCUI_Widget w, LCUI_WidgetEvent e, void *arg)
{
	LCUI_TaskRec task = { 0 };
	TaskController t = &view.detection;

	TaskItem_SetActionDisabled(t->view, TRUE);
	task.func = TaskForStopDetect;
	task.arg[0] = t->task;
	task.arg[1] = t->view;
	t->task = NULL;
	LCUI_PostAsyncTask(&task);
}

void SettingsView_InitDetector(void)
{
	LCUI_Widget btn;
	LCUI_Widget tasks;
	LCUI_Widget task_training = LCUIWidget_New("taskitem");
	LCUI_Widget task_detection = LCUIWidget_New("taskitem");

	SelectWidget(btn, ID_BTN_NEW_MODEL);
	SelectWidget(tasks, ID_VIEW_DETECTOR_TASKS);
	SelectWidget(view.view, ID_VIEW_DETECTOR_SETTING);
	SelectWidget(view.list, ID_VIEW_MODEL_LIST);
	SelectWidget(view.dropdown, ID_DROPDOWN_DETECTOR_MODELS);
	TaskItem_SetIcon(task_detection, "image-search-outline");
	TaskItem_SetNameKey(task_detection, KEY_DETECTOR_DETECTION_TITLE);
	TaskItem_SetTextKey(task_detection, KEY_DETECTOR_DETECTION_TEXT);
	TaskItem_SetIcon(task_training, "brain");
	TaskItem_SetNameKey(task_training, KEY_DETECTOR_TRAINING_TITLE);
	TaskItem_SetTextKey(task_training, KEY_DETECTOR_TRAINING_TEXT);
	Widget_BindEvent(task_training, "start", OnStartTrain, NULL, NULL);
	Widget_BindEvent(task_detection, "start", OnStartDetect, NULL, NULL);
	Widget_BindEvent(task_detection, "stop", OnStopDetect, NULL, NULL);
	Widget_BindEvent(btn, "click", OnBtnNewModelClick, NULL, NULL);
	Widget_Append(tasks, task_detection);
	Widget_Append(tasks, task_training);
	view.detection.view = task_detection;
	view.training.view = task_training;
	RenderModels();
}
