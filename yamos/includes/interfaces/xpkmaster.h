#ifndef XPKMASTER_INTERFACE_DEF_H
#define XPKMASTER_INTERFACE_DEF_H

/*
** This file was machine generated by idltool 50.9.
** Do not edit
*/ 

#ifndef EXEC_TYPES_H
#include <exec/types.h>
#endif
#ifndef EXEC_EXEC_H
#include <exec/exec.h>
#endif
#ifndef EXEC_INTERFACES_H
#include <exec/interfaces.h>
#endif

#ifndef XPK_XPK_H
#include <xpk/xpk.h>
#endif

struct XpkIFace
{
	struct InterfaceData Data;

	ULONG APICALL (*Obtain)(struct XpkIFace *Self);
	ULONG APICALL (*Release)(struct XpkIFace *Self);
	void APICALL (*Expunge)(struct XpkIFace *Self);
	struct Interface * APICALL (*Clone)(struct XpkIFace *Self);
	LONG APICALL (*XpkExamine)(struct XpkIFace *Self, struct XpkFib * fib, struct TagItem * tags);
	LONG APICALL (*XpkExamineTags)(struct XpkIFace *Self, struct XpkFib * fib, ...);
	LONG APICALL (*XpkPack)(struct XpkIFace *Self, struct TagItem * tags);
	LONG APICALL (*XpkPackTags)(struct XpkIFace *Self, ...);
	LONG APICALL (*XpkUnpack)(struct XpkIFace *Self, struct TagItem * tags);
	LONG APICALL (*XpkUnpackTags)(struct XpkIFace *Self, ...);
	LONG APICALL (*XpkOpen)(struct XpkIFace *Self, struct XpkFib ** xbuf, struct TagItem * tags);
	LONG APICALL (*XpkOpenTags)(struct XpkIFace *Self, struct XpkFib ** xbuf, ...);
	LONG APICALL (*XpkRead)(struct XpkIFace *Self, struct XpkFib * xbuf, STRPTR buf, ULONG len);
	LONG APICALL (*XpkWrite)(struct XpkIFace *Self, struct XpkFib * xbuf, STRPTR buf, LONG len);
	LONG APICALL (*XpkSeek)(struct XpkIFace *Self, struct XpkFib * xbuf, LONG len, LONG mode);
	LONG APICALL (*XpkClose)(struct XpkIFace *Self, struct XpkFib * xbuf);
	LONG APICALL (*XpkQuery)(struct XpkIFace *Self, struct TagItem * tags);
	LONG APICALL (*XpkQueryTags)(struct XpkIFace *Self, ...);
	APTR APICALL (*XpkAllocObject)(struct XpkIFace *Self, ULONG type, struct TagItem * tags);
	APTR APICALL (*XpkAllocObjectTags)(struct XpkIFace *Self, ULONG type, ...);
	void APICALL (*XpkFreeObject)(struct XpkIFace *Self, ULONG type, APTR object);
	BOOL APICALL (*XpkPrintFault)(struct XpkIFace *Self, LONG code, STRPTR header);
	ULONG APICALL (*XpkFault)(struct XpkIFace *Self, LONG code, STRPTR header, STRPTR buffer, ULONG size);
	LONG APICALL (*XpkPassRequest)(struct XpkIFace *Self, struct TagItem * tags);
	LONG APICALL (*XpkPassRequestTags)(struct XpkIFace *Self, ...);
};

#endif /* XPKMASTER_INTERFACE_DEF_H */
