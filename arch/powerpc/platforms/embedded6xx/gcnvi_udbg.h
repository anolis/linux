/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Nintendo GameCube/Wii framebuffer udbg output support.
 * Copyright (C) 2008-2009 The GameCube Linux Team
 * Copyright (C) 2008,2009 Albert Herranz
 *
 */

#ifndef _GCNVI_UDBG_H
#define _GCNVI_UDBG_H

#ifdef CONFIG_GAMECUBE_VIDEO_UDBG

extern void __init gcnvi_udbg_init(void);

#else

static inline void __init gcnvi_udbg_init(void)
{
}

#endif /* CONFIG_GAMECUBE_VIDEO_UDBG */

#endif /* _GCNVI_UDBG_H */
