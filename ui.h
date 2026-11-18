#ifndef UI_H
#define UI_H

#include "raylib.h"

/*
 * 功能：窗口创建后加载界面中文字体
 * 输入：font 是接收字体的变量地址
 * 输出：成功时将字体写入 *font，失败时清理本次已加载的资源
 * 返回值：1 成功，0 失败
 */
int loadUiResources(Font *font);

/*
 * 功能：关闭窗口前释放界面字体
 * 输入：font 为加载成功的字体
 * 输出：释放字体资源
 * 返回值：无
 */
void unloadUiResources(Font font);

/*
 * 功能：判断本帧是否点击按钮
 * 输入：bounds 是按钮矩形区域
 * 输出：无，不绘制按钮
 * 返回值：1 本帧左键在按钮区域内按下，0 没有点击
 */
int isButtonClicked(Rectangle bounds);

/*
 * 功能：绘制统一样式的按钮
 * 输入：bounds 为区域，text 为 UTF-8 文字，font 为字体
 * 输出：矩形按钮、边框、文字和悬停效果，不触发任何操作
 * 返回值：无
 */
void drawButton(Rectangle bounds, const char text[], Font font);

#endif
