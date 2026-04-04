#ifndef MOONLIGHT_MOONLIGHT_COMMON_C_COMPAT_H
#define MOONLIGHT_MOONLIGHT_COMMON_C_COMPAT_H

#ifdef NXDK
#ifndef __analysis_assume
#define __analysis_assume(expression) ((void) (expression))
#endif
#endif

#endif
