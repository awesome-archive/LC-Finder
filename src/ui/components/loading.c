﻿/* ***************************************************************************
 * loading.c -- loading
 *
 * Copyright (C) 2019 by Liu Chao <lc-soft@live.cn>
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
 * loading.c -- 加载中显示的提示
 *
 * 版权所有 (C) 2019 归属于 刘超 <lc-soft@live.cn>
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

#include <stdlib.h>
#include <time.h>
#include <LCUI_Build.h>
#include <LCUI/LCUI.h>
#include <LCUI/timer.h>
#include <LCUI/gui/widget.h>
#include <LCUI/gui/widget/textview.h>
#include "ui.h"

#define ANIMATION_FADE_OUT	0
#define ANIMATION_FADE_IN	1

static struct UILoading {
	LCUI_BOOL active;
	LCUI_Widget view;
	LCUI_Widget icon;
	int animation;
	float opactiy;
	int timer;
} loading;

static SetRadomIcon(void *arg)
{
	const char *icon;
	char *icons[] = {
		"cogs",
		"wrench-outline",
		"lightbulb-on-outline",
		"alert-circle-outline",
		"content-copy",
		"cube-send",
		"file-search-outline"
	};

	if (loading.opactiy < 0.5f) {
		loading.opactiy = 0.5f;
		loading.animation = ANIMATION_FADE_IN;
	} else if (loading.opactiy > 1.0f) {
		loading.opactiy = 1.0f;
		loading.animation = ANIMATION_FADE_OUT;
	}
	icon = icons[rand() % 7];
	Widget_SetOpacity(loading.icon, loading.opactiy);
	Widget_SetAttribute(loading.icon, "name", icon);
	if (loading.animation == ANIMATION_FADE_IN) {
		loading.opactiy += 0.1f;
	} else {
		loading.opactiy -= 0.1f;
	}
}

void UI_StartLoading(const wchar_t *message)
{
	LCUI_Widget icon;
	LCUI_Widget text;
	LCUI_Widget box;
	LCUI_Widget dimmer;
	LCUI_Widget window;

	if (loading.active) {
		return;
	}
	box = LCUIWidget_New(NULL);
	dimmer = LCUIWidget_New(NULL);
	icon = LCUIWidget_New("icon");
	text = LCUIWidget_New("textview-i18n");
	window = LCUIWidget_GetById(ID_WINDOW_MAIN);
	Widget_AddClass(box, "loading-box");
	Widget_AddClass(text, "loading-text");
	Widget_AddClass(dimmer, "loading-dimmer");
	Widget_AddClass(icon, "loading-icon");
	Widget_Append(box, icon);
	Widget_Append(box, text);
	Widget_Append(dimmer, box);
	Widget_Append(window, dimmer);
	TextView_SetTextW(text, message);
	loading.opactiy = 1.0f;
	loading.icon = icon;
	loading.view = dimmer;
	loading.active = TRUE;
	loading.animation = ANIMATION_FADE_OUT;
	loading.timer = LCUI_SetInterval(100, SetRadomIcon, NULL);
	SetRadomIcon(NULL);
	srand((unsigned)time(NULL));
}

void UI_StopLoading(void)
{
	if (!loading.active) {
		return;
	}
	Widget_Destroy(loading.view);
	LCUITimer_Free(loading.timer);
	loading.timer = 0;
	loading.view = NULL;
	loading.active = FALSE;
}
