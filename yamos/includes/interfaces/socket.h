#ifndef SOCKET_INTERFACE_DEF_H
#define SOCKET_INTERFACE_DEF_H

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

#ifndef LIBRARIES_SOCKET_H
#include <libraries/socket.h>
#endif

struct SocketIFace
{
	struct InterfaceData Data;

	ULONG APICALL (*Obtain)(struct SocketIFace *Self);
	ULONG APICALL (*Release)(struct SocketIFace *Self);
	void APICALL (*Expunge)(struct SocketIFace *Self);
	struct Interface * APICALL (*Clone)(struct SocketIFace *Self);
	LONG APICALL (*Socket)(struct SocketIFace *Self, LONG par1, LONG par2, LONG last);
	void APICALL (*Reserved1)(struct SocketIFace *Self);
	void APICALL (*Reserved2)(struct SocketIFace *Self);
	void APICALL (*Reserved3)(struct SocketIFace *Self);
	LONG APICALL (*Connect)(struct SocketIFace *Self, LONG par1, const struct sockaddr * par2, LONG last);
	void APICALL (*Reserved4)(struct SocketIFace *Self);
	LONG APICALL (*Send)(struct SocketIFace *Self, LONG par1, const unsigned char * par2, LONG par3, LONG last);
	void APICALL (*Reserved5)(struct SocketIFace *Self);
	LONG APICALL (*Recv)(struct SocketIFace *Self, LONG par1, unsigned char * par2, LONG par3, LONG last);
	LONG APICALL (*Shutdown)(struct SocketIFace *Self, LONG par1, LONG last);
	void APICALL (*Reserved6)(struct SocketIFace *Self);
	void APICALL (*Reserved7)(struct SocketIFace *Self);
	void APICALL (*Reserved8)(struct SocketIFace *Self);
	void APICALL (*Reserved9)(struct SocketIFace *Self);
	LONG APICALL (*IoctlSocket)(struct SocketIFace *Self, LONG par1, ULONG par2, char * last);
	LONG APICALL (*CloseSocket)(struct SocketIFace *Self, LONG last);
	void APICALL (*Reserved10)(struct SocketIFace *Self);
	void APICALL (*Reserved11)(struct SocketIFace *Self);
	void APICALL (*Reserved12)(struct SocketIFace *Self);
	LONG APICALL (*ObtainSocket)(struct SocketIFace *Self, LONG par1, LONG par2, LONG par3, LONG last);
	LONG APICALL (*ReleaseSocket)(struct SocketIFace *Self, LONG par1, LONG last);
	LONG APICALL (*ReleaseCopyOfSocket)(struct SocketIFace *Self, LONG par1, LONG last);
	LONG APICALL (*Errno)(struct SocketIFace *Self);
	LONG APICALL (*SetErrnoPtr)(struct SocketIFace *Self, void * par1, LONG last);
	char * APICALL (*Inet_NtoA)(struct SocketIFace *Self, ULONG last);
	void APICALL (*Reserved13)(struct SocketIFace *Self);
	ULONG APICALL (*Inet_LnaOf)(struct SocketIFace *Self, LONG last);
	ULONG APICALL (*Inet_NetOf)(struct SocketIFace *Self, LONG last);
	ULONG APICALL (*Inet_MakeAddr)(struct SocketIFace *Self, ULONG par1, ULONG last);
	void APICALL (*Reserved14)(struct SocketIFace *Self);
	struct hostent * APICALL (*GetHostByName)(struct SocketIFace *Self, const unsigned char * last);
	void APICALL (*Reserved15)(struct SocketIFace *Self);
	void APICALL (*Reserved16)(struct SocketIFace *Self);
	void APICALL (*Reserved17)(struct SocketIFace *Self);
	void APICALL (*Reserved18)(struct SocketIFace *Self);
	void APICALL (*Reserved19)(struct SocketIFace *Self);
	void APICALL (*Reserved20)(struct SocketIFace *Self);
	void APICALL (*Reserved21)(struct SocketIFace *Self);
	void APICALL (*Reserved22)(struct SocketIFace *Self);
	LONG APICALL (*Dup2Socket)(struct SocketIFace *Self, LONG par1, LONG last);
	void APICALL (*Reserved23)(struct SocketIFace *Self);
	void APICALL (*Reserved24)(struct SocketIFace *Self);
	void APICALL (*Reserved25)(struct SocketIFace *Self);
	void APICALL (*Reserved26)(struct SocketIFace *Self);
	LONG APICALL (*SocketBaseTagList)(struct SocketIFace *Self, struct TagItem * last);
	LONG APICALL (*SocketBaseTags)(struct SocketIFace *Self, ...);
	LONG APICALL (*GetSocketEvents)(struct SocketIFace *Self, ULONG * last);
};

#endif /* SOCKET_INTERFACE_DEF_H */
