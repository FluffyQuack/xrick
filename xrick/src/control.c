/*
 * xrick/src/control.c
 *
 * Copyright (C) 1998-2019 bigorno (bigorno@bigorno.net). All rights reserved.
 *
 * The use and distribution terms for this software are contained in the file
 * named README, which can be found in the root of this distribution. By
 * using this software in any fashion, you are agreeing to be bound by the
 * terms of this license.
 *
 * You must not remove this notice, or any other, from this software.
 */

#include "control.h"

/*
 * Co-op (Stage 2): one CONTROL_* byte per player. control_status_p[0] is
 * P1 (the legacy `control_status` macro maps here). All four slots exist
 * regardless of rick_count -- inactive slots simply never have bits set.
 */
U8 control_status_p[CONTROL_PLAYERS] = { 0, 0, 0, 0 };

U8 control_last = 0;
U8 control_active = TRUE;

/* eof */


