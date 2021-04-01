﻿/* ***************************************************************************
 * view_search.c -- search view
 *
 * Copyright (C) 2016-2018 by Liu Chao <lc-soft@live.cn>
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
 * view_search.c -- “搜索”视图
 *
 * 版权所有 (C) 2016-2018 归属于 刘超 <lc-soft@live.cn>
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

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "finder.h"
#include "ui.h"
#include <LCUI/timer.h>
#include <LCUI/display.h>
#include <LCUI/gui/widget.h>
#include <LCUI/gui/widget/textview.h>
#include <LCUI/gui/widget/textedit.h>
#include <LCUI/util/charset.h>
#include "file_storage.h"
#include "file_stage.h"
#include "thumbview.h"
#include "textview_i18n.h"
#include "tagthumb.h"
#include "browser.h"
#include "i18n.h"

/* clang-format off */

#define KEY_SORT_HEADER		"sort.header"
#define KEY_TITLE		"search.results.title"
#define KEY_INPUT_PLACEHOLDER	"search.placeholder.search_input"
#define TAG_MAX_WIDTH		180
#define TAG_MARGIN_RIGHT	15
#define SORT_METHODS_LEN	6

typedef enum ViewState {
	STATE_NORMAL,
	STATE_SEARCH
} ViewState;

/** 文件扫描功能的相关数据 */
typedef struct FileScannerRec_ {
	LCUI_Thread tid;
	LCUI_BOOL is_running;

	int timer;

	FileStage stage;
	LinkedList files;

	DB_Tag *tags;
	size_t n_tags;
} FileScannerRec, *FileScanner;

static struct SearchView {
	LCUI_Widget input;
	LCUI_Widget view;
	LCUI_Widget view_tags;
	LCUI_Widget view_tags_wrapper;
	LCUI_Widget view_results_wrapper;
	LCUI_Widget view_files;
	LCUI_Widget txt_count;
	LCUI_Widget btn_search;
	LCUI_Widget btn_clear;
	LCUI_Widget tip_empty_tags;
	LCUI_Widget tip_empty_files;
	LCUI_Widget selected_sort;
	LCUI_Widget navbar_actions;
	LCUI_BOOL need_update;
	struct {
		int count;
		int tags_per_row;
		float max_width;
	} layout;
	int sort_mode;
	ViewState state;
	LinkedList tags;
	FileScannerRec scanner;
	FileBrowserRec browser;
	DB_QueryTermsRec terms;
} search_view = { 0 };

static FileSortMethodRec sort_methods[SORT_METHODS_LEN] = {
	{ "sort.ctime_desc", CREATE_TIME_DESC },
	{ "sort.ctime_asc", CREATE_TIME_ASC },
	{ "sort.mtime_desc", MODIFY_TIME_DESC },
	{ "sort.mtime_asc", MODIFY_TIME_ASC },
	{ "sort.score_desc", SCORE_DESC },
	{ "sort.score_asc", SCORE_ASC }
};

typedef struct TagItemRec_ {
	LCUI_Widget widget;
	DB_Tag tag;
} TagItemRec, *TagItem;

/* clang-format on */

static void OnDeleteDBFile(void *arg)
{
	DBFile_Release(arg);
}

static void SearchView_AppendFiles(void *arg)
{
	LinkedList files;
	LinkedListNode *node;
	FileScanner scanner = &search_view.scanner;

	LinkedList_Init(&files);
	FileStage_GetFiles(scanner->stage, &files, 512);
	for (LinkedList_Each(node, &files)) {
		FileBrowser_AppendPicture(&search_view.browser, node->data);
	}
	LinkedList_Concat(&scanner->files, &files);
	if (!scanner->is_running) {
		scanner->timer = 0;
		return;
	}
	scanner->timer = LCUI_SetTimeout(200, SearchView_AppendFiles, NULL);
}

static size_t FileScanner_ScanAll(FileScanner scanner)
{
	DB_File file;
	DB_Query query;
	DB_QueryTerms terms;
	size_t i, total, count;

	terms = &search_view.terms;
	terms->offset = 0;
	terms->limit = 512;
	terms->tags = scanner->tags;
	terms->n_tags = scanner->n_tags;
	if (terms->dirs) {
		size_t n_dirs;
		free(terms->dirs);
		terms->dirs = NULL;
		n_dirs = LCFinder_GetSourceDirList(&terms->dirs);
		if (n_dirs == finder.n_dirs) {
			free(terms->dirs);
			terms->dirs = NULL;
		}
	}

	query = DB_NewQuery(&search_view.terms);
	total = DBQuery_GetTotalFiles(query);
	DB_DeleteQuery(query);

	for (count = 0; scanner->is_running && count < total;) {
		query = DB_NewQuery(&search_view.terms);
		for (i = 0; scanner->is_running; ++count, ++i) {
			file = DBQuery_FetchFile(query);
			if (!file) {
				break;
			}
			FileStage_AddFile(scanner->stage, file);
			++count;
		}
		terms->offset += terms->limit;
		FileStage_Commit(scanner->stage);
		DB_DeleteQuery(query);
		if (i < terms->limit) {
			break;
		}
	}
	if (terms->dirs) {
		free(terms->dirs);
		terms->dirs = NULL;
	}
	FileStage_Commit(scanner->stage);
	return total;
}

static void FileScanner_Init(FileScanner scanner)
{
	scanner->tags = NULL;
	scanner->n_tags = 0;
	scanner->stage = FileStage_Create();
	LinkedList_Init(&scanner->files);
}

static void FileScanner_Reset(FileScanner scanner)
{
	if (scanner->is_running) {
		scanner->is_running = FALSE;
		LCUIThread_Join(scanner->tid, NULL);
	}
	if (scanner->timer) {
		LCUITimer_Free(scanner->timer);
		scanner->timer = 0;
	}
	FileStage_GetFiles(scanner->stage, &scanner->files, 0);
	LinkedList_Clear(&scanner->files, OnDeleteDBFile);
}

static void FileScanner_Thread(void *arg)
{
	size_t count;

	search_view.scanner.is_running = TRUE;
	count = FileScanner_ScanAll(&search_view.scanner);
	if (count > 0) {
		Widget_AddClass(search_view.tip_empty_files, "hide");
		Widget_Hide(search_view.tip_empty_files);
	} else {
		Widget_RemoveClass(search_view.tip_empty_files, "hide");
		Widget_Show(search_view.tip_empty_files);
	}
	search_view.scanner.is_running = FALSE;
	LCUIThread_Exit(NULL);
}

static void FileScanner_Start(FileScanner scanner)
{
	FileScanner_Reset(scanner);
	scanner->timer = LCUI_SetTimeout(200, SearchView_AppendFiles, NULL);
	LCUIThread_Create(&scanner->tid, FileScanner_Thread, NULL);
}

static void FileScanner_Destroy(FileScanner scanner)
{
	FileScanner_Reset(scanner);
	FileStage_Destroy(scanner->stage);
}

static void UpdateLayoutContext(void)
{
	double n;
	float max_width;
	max_width = search_view.view_tags->parent->box.content.width;
	n = max_width + TAG_MARGIN_RIGHT;
	n = ceil(n / (TAG_MAX_WIDTH + TAG_MARGIN_RIGHT));
	search_view.layout.max_width = max_width;
	search_view.layout.tags_per_row = (int)n;
}

static void UpdateSearchResults(void)
{
	FileScanner_Reset(&search_view.scanner);
	FileBrowser_Empty(&search_view.browser);
	FileScanner_Start(&search_view.scanner);
}

static void UpdateSearchInputPlaceholder(void)
{
	const wchar_t *text = I18n_GetText(KEY_INPUT_PLACEHOLDER);
	TextEdit_SetPlaceHolderW(search_view.input, text);
}

static void UpdateSearchInputActions(void)
{
	size_t len = TextEdit_GetTextLength(search_view.input);
	if (len > 0) {
		Widget_Show(search_view.btn_clear);
	} else {
		Widget_Hide(search_view.btn_clear);
	}
}

static void OnTagViewStartLayout(LCUI_Widget w)
{
	search_view.layout.count = 0;
	search_view.layout.tags_per_row = 2;
	UpdateLayoutContext();
}

static void UnselectAllTags(void)
{
	LinkedListNode *node;
	for (LinkedList_Each(node, &search_view.tags)) {
		TagItem item = node->data;
		Widget_RemoveClass(item->widget, "selected");
	}
}

static void AddTagToSearch(LCUI_Widget w)
{
	size_t len;
	DB_Tag tag = NULL;
	LinkedListNode *node;
	wchar_t *tagname, text[512];

	for (LinkedList_Each(node, &search_view.tags)) {
		TagItem item = node->data;
		if (item->widget == w) {
			tag = item->tag;
			break;
		}
	}
	if (!tag) {
		return;
	}
	len = strlen(tag->name) + 1;
	tagname = NEW(wchar_t, len);
	LCUI_DecodeUTF8String(tagname, tag->name, len);
	len = TextEdit_GetTextW(search_view.input, 0, 511, text);
	if (len > 0 && wcsstr(text, tagname)) {
		free(tagname);
		return;
	}
	if (len == 0) {
		wcsncpy(text, tagname, 511);
	} else {
		swprintf(text, 511, L" %ls", tagname);
	}
	TextEdit_AppendTextW(search_view.input, text);
	UpdateSearchInputActions();
	free(tagname);
}

static void DeleteTagFromSearch(LCUI_Widget w)
{
	size_t len, taglen;
	DB_Tag tag = NULL;
	LinkedListNode *node;
	wchar_t *tagname, text[512], *p, *pend;

	for (LinkedList_Each(node, &search_view.tags)) {
		TagItem item = node->data;
		if (item->widget == w) {
			tag = item->tag;
			break;
		}
	}
	if (!tag) {
		return;
	}
	len = strlen(tag->name) + 1;
	tagname = NEW(wchar_t, len);
	taglen = LCUI_DecodeUTF8String(tagname, tag->name, len);
	len = TextEdit_GetTextW(search_view.input, 0, 511, text);
	if (len == 0) {
		return;
	}
	p = wcsstr(text, tagname);
	if (!p) {
		return;
	}
	/* 将标签名后面的空格数量也算入标签长度内，以清除多余空格 */
	for (pend = p + taglen; *pend && *pend == L' '; ++pend, ++taglen)
		;
	pend = &text[len - taglen];
	for (; p < pend; ++p) {
		*p = *(p + taglen);
	}
	*pend = 0;
	TextEdit_SetTextW(search_view.input, text);
	free(tagname);
}

static void OnTagClick(LCUI_Widget w, LCUI_WidgetEvent e, void *arg)
{
	if (Widget_HasClass(w, "selected")) {
		Widget_RemoveClass(w, "selected");
		DeleteTagFromSearch(w);
	} else {
		Widget_AddClass(w, "selected");
		AddTagToSearch(w);
	}
}

static void SetTagCover(LCUI_Widget w, LCUI_Graph *thumb)
{
	TagThumb_SetCover(w, thumb);
}

static void UnsetTagCover(LCUI_Widget w)
{
	DEBUG_MSG("remove thumb\n");
	TagThumb_RemoveCover(w);
}

static void UpdateTagSize(LCUI_Widget w)
{
	int n;
	float width;
	++search_view.layout.count;
	if (search_view.layout.count == 1) {
		UpdateLayoutContext();
	}
	if (search_view.layout.max_width < TAG_MARGIN_RIGHT) {
		return;
	}
	n = search_view.layout.tags_per_row;
	width = search_view.layout.max_width;
	width = (width - TAG_MARGIN_RIGHT * (n - 1)) / n;
	/* 设置每行最后一个文件夹的右边距为 0px */
	if (search_view.layout.count % n == 0) {
		Widget_SetStyle(w, key_margin_right, 0, px);
	} else {
		Widget_UnsetStyle(w, key_margin_right);
	}
	Widget_SetStyle(w, key_width, width, px);
	Widget_UpdateStyle(w, FALSE);
}

static DB_File GetFileByTag(DB_Tag tag)
{
	DB_File file;
	DB_Query query;
	DB_QueryTermsRec terms = { 0 };
	terms.tags = malloc(sizeof(DB_Tag));
	terms.tags[0] = tag;
	terms.n_dirs = 1;
	terms.n_tags = 1;
	terms.limit = 1;
	terms.modify_time = DESC;
	query = DB_NewQuery(&terms);
	file = DBQuery_FetchFile(query);
	free(terms.tags);
	return file;
}

static LCUI_Widget CreateTagWidget(DB_Tag tag)
{
	DB_File file;
	LCUI_Widget w;

	file = GetFileByTag(tag);
	w = LCUIWidget_New("tagthumb");

	TagThumb_SetName(w, tag->name);
	TagThumb_SetCount(w, tag->count);

	Widget_Append(search_view.view_tags, w);
	Widget_BindEvent(w, "click", OnTagClick, tag, NULL);

	ThumbViewItem_BindFile(w, file);
	ThumbViewItem_SetFunction(w, SetTagCover, UnsetTagCover, UpdateTagSize);
	return w;
}

static void OnTagUpdate(void *data, void *arg)
{
	search_view.need_update = TRUE;
}

static void OnSidebarBtnClick(LCUI_Widget w, LCUI_WidgetEvent e, void *arg)
{
	if (search_view.need_update) {
		UI_UpdateSearchView();
	}
}

static void StartSearchFiles(LinkedList *tags)
{
	size_t i;
	size_t n_tags = 0;

	DB_Tag tag;
	DB_Tag *newtags;
	LinkedListNode *node;

	newtags = malloc(sizeof(DB_Tag) * (tags->length + 1));
	if (!newtags) {
		return;
	}
	for (LinkedList_Each(node, tags)) {
		for (i = 0; i < finder.n_tags; ++i) {
			tag = finder.tags[i];
			if (strcmp(tag->name, node->data) == 0) {
				newtags[n_tags++] = tag;
			}
		}
	}
	newtags[n_tags] = NULL;
	FileScanner_Reset(&search_view.scanner);
	if (search_view.scanner.tags) {
		free(search_view.scanner.tags);
	}
	search_view.scanner.tags = newtags;
	search_view.scanner.n_tags = n_tags;
	FileBrowser_Empty(&search_view.browser);
	FileScanner_Start(&search_view.scanner);
}

static void OnBtnSearchClick(LCUI_Widget w, LCUI_WidgetEvent e, void *arg)
{
	size_t i, j, len;
	LinkedList tags;
	LCUI_BOOL saving;
	wchar_t wstr[512] = { 0 };
	char *str, buf[512], *tagname;

	len = TextEdit_GetTextW(search_view.input, 0, 511, wstr);
	if (len == 0) {
		return;
	}
	len = LCUI_EncodeUTF8String(NULL, wstr, 0) + 1;
	str = malloc(sizeof(char) * len);
	if (!str) {
		return;
	}
	LinkedList_Init(&tags);
	len = LCUI_EncodeString(str, wstr, len, ENCODING_UTF8);
	for (saving = FALSE, i = 0, j = 0; i <= len; ++i) {
		if (str[i] != ' ' && str[i] != 0) {
			saving = TRUE;
			buf[j++] = str[i];
			continue;
		}
		if (saving) {
			buf[j++] = 0;
			tagname = malloc(sizeof(char) * j);
			strcpy(tagname, buf);
			LinkedList_Append(&tags, tagname);
			j = 0;
		}
		saving = FALSE;
	}
	if (tags.length == 0) {
		return;
	}
	search_view.state = STATE_SEARCH;
	Widget_RemoveClass(search_view.view_results_wrapper, "hide");
	Widget_AddClass(search_view.view_tags_wrapper, "hide");
	Widget_RemoveClass(search_view.navbar_actions, "hide");
	StartSearchFiles(&tags);
	LinkedList_Clear(&tags, free);
}

static void UpdateViewState(void)
{
	if (search_view.need_update) {
		UI_UpdateSearchView();
	}
	if (search_view.state != STATE_NORMAL &&
	    TextEdit_GetTextLength(search_view.input) < 1) {
		UnselectAllTags();
		Widget_AddClass(search_view.view_results_wrapper, "hide");
		Widget_AddClass(search_view.navbar_actions, "hide");
		Widget_RemoveClass(search_view.view_tags_wrapper, "hide");
		ThumbView_Lock(search_view.view_files);
		ThumbView_Empty(search_view.view_files);
		ThumbView_Unlock(search_view.view_files);
		search_view.state = STATE_NORMAL;
	}
}

static void OnBtnClearClick(LCUI_Widget w, LCUI_WidgetEvent e, void *arg)
{
	TextEdit_ClearText(search_view.input);
	UpdateSearchInputActions();
	UpdateViewState();
}

static void OnSearchInput(LCUI_Widget w, LCUI_WidgetEvent e, void *arg)
{
	UpdateSearchInputActions();
	UpdateViewState();
}

void UI_UpdateSearchView(void)
{
	size_t i, count;
	LCFinder_ReloadTags();
	search_view.layout.count = 0;
	ThumbView_Empty(search_view.view_tags);
	LinkedList_Clear(&search_view.tags, free);
	for (i = 0, count = 0; i < finder.n_tags; ++i) {
		TagItem item;
		if (finder.tags[i]->count <= 0) {
			continue;
		}
		item = NEW(TagItemRec, 1);
		item->tag = finder.tags[i];
		item->widget = CreateTagWidget(finder.tags[i]);
		ThumbView_Append(search_view.view_tags, item->widget);
		LinkedList_Append(&search_view.tags, item);
		++count;
	}
	if (count > 0) {
		Widget_Hide(search_view.tip_empty_tags);
		Widget_AddClass(search_view.tip_empty_tags, "hide");
		Widget_SetDisabled(search_view.btn_search, FALSE);
		Widget_SetDisabled(search_view.input, FALSE);
	} else {
		TextEdit_ClearText(search_view.input);
		Widget_Show(search_view.tip_empty_tags);
		Widget_RemoveClass(search_view.tip_empty_tags, "hide");
		Widget_SetDisabled(search_view.btn_search, TRUE);
		Widget_SetDisabled(search_view.input, TRUE);
	}
	search_view.need_update = FALSE;
}

static void UpdateQueryTerms(void)
{
	search_view.terms.modify_time = NONE;
	search_view.terms.create_time = NONE;
	search_view.terms.score = NONE;
	switch (search_view.sort_mode) {
	case CREATE_TIME_DESC:
		search_view.terms.create_time = DESC;
		break;
	case CREATE_TIME_ASC:
		search_view.terms.create_time = ASC;
		break;
	case SCORE_DESC:
		search_view.terms.score = DESC;
		break;
	case SCORE_ASC:
		search_view.terms.score = ASC;
		break;
	case MODIFY_TIME_ASC:
		search_view.terms.modify_time = ASC;
		break;
	case MODIFY_TIME_DESC:
	default:
		search_view.terms.modify_time = DESC;
		break;
	}
}

static void OnSelectSortMethod(LCUI_Widget w, LCUI_WidgetEvent e, void *arg)
{
	FileSortMethod sort;

	sort = (FileSortMethod)Widget_GetAttribute(e->target, "value");
	if (search_view.sort_mode == sort->value) {
		return;
	}
	search_view.sort_mode = sort->value;
	if (search_view.selected_sort) {
		Widget_RemoveClass(search_view.selected_sort, "selected");
	}
	search_view.selected_sort = e->target;
	Widget_AddClass(e->target, "selected");
	UpdateQueryTerms();
	UpdateSearchResults();
}

static void InitSearchResultsSort(void)
{
	int i;
	LCUI_Widget menu, item, icon, text;

	SelectWidget(menu, ID_DROPDOWN_SEARCH_FILES_SORT);
	Widget_Empty(menu);

	item = LCUIWidget_New("textview-i18n");
	TextViewI18n_SetKey(item, KEY_SORT_HEADER);
	Widget_AddClass(item, "dropdown-header");
	Widget_Append(menu, item);

	for (i = 0; i < SORT_METHODS_LEN; ++i) {
		FileSortMethod sort = &sort_methods[i];
		item = LCUIWidget_New(NULL);
		icon = LCUIWidget_New("textview");
		text = LCUIWidget_New("textview-i18n");
		TextViewI18n_SetKey(text, sort->name_key);
		Widget_SetAttributeEx(item, "value", sort, 0, NULL);
		Widget_AddClass(item, "dropdown-item");
		Widget_AddClass(icon, "icon icon-check");
		Widget_AddClass(text, "text");
		Widget_Append(item, icon);
		Widget_Append(item, text);
		Widget_Append(menu, item);
		if (sort->value == finder.config.files_sort) {
			Widget_AddClass(item, "selected");
			search_view.selected_sort = item;
		}
	}
	BindEvent(menu, "change.dropdown", OnSelectSortMethod);
	UpdateQueryTerms();
}

static void OnTagThumbViewReady(LCUI_Widget w, LCUI_WidgetEvent e, void *arg)
{
	UI_UpdateSearchView();
}

static void InitSearchInput(void)
{
	SelectWidget(search_view.input, ID_INPUT_SEARCH);
	SelectWidget(search_view.btn_search, ID_BTN_SEARCH_FILES);
	SelectWidget(search_view.btn_clear, ID_BTN_CLEAR_SEARCH);
	BindEvent(search_view.btn_search, "click", OnBtnSearchClick);
	BindEvent(search_view.btn_clear, "click", OnBtnClearClick);
	BindEvent(search_view.input, "change", OnSearchInput);
	LCFinder_BindEvent(EVENT_LANG_CHG,
			   (LCFinder_EventHandler)UpdateSearchInputPlaceholder,
			   NULL);
	UpdateSearchInputPlaceholder();
	UpdateSearchInputActions();
}

static void InitView(void)
{
	search_view.state = STATE_NORMAL;
	SelectWidget(search_view.navbar_actions, ID_SEARCH_ACTIONS);
	SelectWidget(search_view.tip_empty_tags, ID_TIP_SEARCH_TAGS_EMPTY);
	SelectWidget(search_view.tip_empty_files, ID_TIP_SEARCH_FILES_EMPTY);
	SelectWidget(search_view.view_tags, ID_VIEW_SEARCH_TAGS);
	SelectWidget(search_view.view_tags_wrapper,
		     ID_VIEW_SEARCH_TAGS_WRAPPER);
	SelectWidget(search_view.view_results_wrapper,
		     ID_VIEW_SEARCH_RESULTS_WRAPPER);
	SelectWidget(search_view.view_files, ID_VIEW_SEARCH_FILES);
	SelectWidget(search_view.view, ID_VIEW_SEARCH);
}

static void InitBrowser(void)
{
	LCUI_Widget title;
	LCUI_Widget btns[6];

	search_view.layout.count = 0;
	search_view.layout.tags_per_row = 2;
	SelectWidget(btns[0], ID_BTN_SIDEBAR_SEARCH);
	SelectWidget(btns[1], ID_BTN_SELECT_SEARCH_FILES);
	SelectWidget(btns[2], ID_BTN_CANCEL_SEARCH_SELECT);
	SelectWidget(btns[3], ID_BTN_TAG_SEARCH_FILES);
	SelectWidget(btns[4], ID_BTN_DELETE_SEARCH_FILES);
	SelectWidget(title, ID_TXT_SEARCH_SELECTION_STATS);
	search_view.browser.btn_select = btns[1];
	search_view.browser.btn_cancel = btns[2];
	search_view.browser.btn_tag = btns[3];
	search_view.browser.btn_delete = btns[4];
	search_view.browser.txt_selection_stats = title;
	search_view.browser.view = search_view.view;
	search_view.browser.items = search_view.view_files;
	ThumbView_SetCache(search_view.view_tags, finder.thumb_cache);
	ThumbView_SetCache(search_view.view_files, finder.thumb_cache);
	ThumbView_SetStorage(search_view.view_tags, finder.storage_for_thumb);
	ThumbView_SetStorage(search_view.view_files, finder.storage_for_thumb);
	ThumbView_OnLayout(search_view.view_tags, OnTagViewStartLayout);
	BindEvent(btns[0], "click", OnSidebarBtnClick);
	FileBrowser_Init(&search_view.browser);
	if (search_view.view_tags->state == LCUI_WSTATE_NORMAL) {
		UI_UpdateSearchView();
	} else {
		BindEvent(search_view.view_tags, "ready", OnTagThumbViewReady);
	}
	LCFinder_BindEvent(EVENT_TAG_UPDATE, OnTagUpdate, NULL);
}

static void InitFileScanner(void)
{
	LinkedList_Init(&search_view.tags);
	FileScanner_Init(&search_view.scanner);
}

void UI_InitSearchView(void)
{
	InitView();
	InitSearchInput();
	InitFileScanner();
	InitBrowser();
	InitSearchResultsSort();
}

void UI_ExitSearchView(void)
{
	FileScanner_Reset(&search_view.scanner);
	FileScanner_Destroy(&search_view.scanner);
}
