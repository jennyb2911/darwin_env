/*
 * Copyright (c) 2000-2004 Apple Computer, Inc. All Rights Reserved.
 * 
 * @APPLE_LICENSE_HEADER_START@
 * 
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 * 
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 * 
 * @APPLE_LICENSE_HEADER_END@
 */
#ifndef _H_UCSP_TYPES
#define _H_UCSP_TYPES


//
// ucsp_types - type equivalence declarations for SecurityServer MIG
//
#include "ss_types.h"

namespace Security {

using namespace SecurityServer;

typedef DataWalkers::DLDbFlatIdentifier DLDbIdentBlob;
typedef DataWalkers::DLDbFlatIdentifier *DLDbIdentPtr;

typedef AuthorizationItemSet AuthorizationItemSetBlob;
typedef AuthorizationItemSet *AuthorizationItemSetPtr;
typedef void *AuthorizationHandle;


//
// Customization macros for MIG code
//
#define __AfterSendRpc(id, name) \
	if (msg_result == MACH_MSG_SUCCESS && Out0P->Head.msgh_id == MACH_NOTIFY_DEAD_NAME) \
		return MIG_SERVER_DIED;

#define UseStaticTemplates 0


} // end namespace Security

#endif //_H_UCSP_TYPES
