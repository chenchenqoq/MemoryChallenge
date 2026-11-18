#ifndef RANK_H
#define RANK_H

#include "raylib.h"

/*
 * 功能：提交指定难度的通关成绩
 * 输入：name 为用户名，level 为 1、2、3，seconds 为非负整秒用时
 * 输出：需要更新时修改排行榜并保存到 ranks.txt
 * 返回值：1 更新且保存成功，0 未刷新个人最好成绩或未进入前五
 *        -1 参数或文件读写错误
 */
int saveScore(const char name[], int level, int seconds);

/*
 * 功能：准备排行榜页面
 * 输入：无
 * 输出：默认选择初级，准备显示成绩或读取错误提示
 * 返回值：无
 */
void resetRankPage(void);

/*
 * 功能：处理难度选择和返回按钮
 * 输入：无，通过本帧鼠标操作
 * 输出：修改当前显示的难度
 * 返回值：0 留在排行榜，1 返回玩家菜单
 */
int updateRankPage(void);

/*
 * 功能：绘制当前难度排行榜
 * 输入：font 为加载成功的中文字体
 * 输出：名次、用户名、用时，或暂无成绩、读取失败提示
 * 返回值：无
 */
void drawRankPage(Font font);

#endif
