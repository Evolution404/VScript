#define vinit_c

#include <stddef.h>

#include "vs.h"

#include "vslib.h"
#include "vauxlib.h"


/*
** these libs are loaded by vs.c and are readily available to any VS
** program
*/
// 初始化要加载的模块
// 这里模块都被加载成全局变量 挂载到_G表下
// 例如访问math模块就在_G.math下, base模块名称是_G所以直接暴露出来不需要模块名
static const vsL_Reg loadedlibs[] = {
  {"_G", vsopen_base},
//  {VS_TABLIBNAME, vsopen_table},
//  {VS_IOLIBNAME, vsopen_io},
//  {VS_OSLIBNAME, vsopen_os},
//  {VS_STRLIBNAME, vsopen_string},
//  {VS_MATHLIBNAME, vsopen_math},
  {NULL, NULL}
};


// 遍历loadedlibs加载所有模块
VSLIB_API void vsL_openlibs (vs_State *L) {
  const vsL_Reg *lib;
  /* "require" functions from 'loadedlibs' and set results to global table */
  // 遍历上面的loadedlibs数组
  for (lib = loadedlibs; lib->func; lib++) {
    vsL_requiref(L, lib->name, lib->func, 1);
    // vsL_requiref执行后栈顶留下了加载的module,在这里删除掉
    vs_pop(L, 1);  /* remove lib */
  }
}

