﻿#ifndef LCFINDER_TYPES_H
#define LCFINDER_TYPES_H

/** 文件迭代器 */
typedef struct FileIteratorRec_* FileIterator;
typedef struct FileIteratorRec_ {
	size_t index;			/**< 当前索引位置 */
	size_t length;			/**< 文件列表总长度 */
	void *privdata;			/**< 私有数据 */
	char *filepath;			/**< 文件路径 */
	void(*unlink)(FileIterator);	/**< 将当前文件从列表中移除 */
	void(*next)(FileIterator);	/**< 切换至下一个文件 */
	void(*prev)(FileIterator);	/**< 切换至上一个文件 */
	void(*destroy)(FileIterator);	/**< 销毁文件迭代器 */
} FileIteratorRec;

typedef void(*FileIteratorFunc)(FileIterator);

#endif
