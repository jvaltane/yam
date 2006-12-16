/***********-****************************************************************

 YAM - Yet Another Mailer
 Copyright (C) 1995-2000 by Marcel Beck <mbeck@yam.ch>
 Copyright (C) 2000-2006 by YAM Open Source Team

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

 YAM Official Support Site :  http://www.yam.ch
 YAM OpenSource project    :  http://sourceforge.net/projects/yamos/

 $Id$

***************************************************************************/

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <clib/alib_protos.h>
#include <clib/macros.h>
#include <libraries/asl.h>
#include <libraries/iffparse.h>
#include <libraries/gadtools.h>
#include <mui/NList_mcc.h>
#include <mui/NListview_mcc.h>
#include <mui/TextEditor_mcc.h>
#include <proto/codesets.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/utility.h>

#include "extrasrc.h"

#include "SDI_hook.h"

#include "YAM.h"
#include "YAM_addressbook.h"
#include "YAM_addressbookEntry.h"
#include "YAM_config.h"
#include "YAM_error.h"
#include "YAM_folderconfig.h"
#include "YAM_global.h"
#include "YAM_locale.h"
#include "YAM_mail_lex.h"
#include "YAM_main.h"
#include "YAM_mainFolder.h"
#include "YAM_mime.h"
#include "YAM_read.h"
#include "YAM_write.h"
#include "YAM_utilities.h"
#include "HTML2Mail.h"

#include "classes/Classes.h"

#include "Debug.h"

/***************************************************************************
 Module: Read
***************************************************************************/

/// RE_GetThread
//  Function that find the next/prev message in a thread and returns a pointer to it
struct Mail *RE_GetThread(struct Mail *srcMail, BOOL nextThread, BOOL askLoadAllFolder, Object *readWindow)
{
   struct Folder **flist;
   struct Mail *mail = NULL;
   BOOL found = FALSE;

   if(srcMail)
   {
      // first we take the folder of the srcMail as a priority in the
      // search of the next/prev thread so we have to check that we
      // have a valid index before we are going to go on.
      if(srcMail->Folder->LoadedMode != LM_VALID && MA_GetIndex(srcMail->Folder))
      {
        return NULL;
      }

      // ok the folder is valid and we can scan it now
      for(mail = srcMail->Folder->Messages; mail; mail = mail->Next)
      {
        if(nextThread) // find the answer to the srcMail
        {
          if(mail->cIRTMsgID && mail->cIRTMsgID == srcMail->cMsgID)
          {
            found = TRUE;
            break;
          }
        }
        else // else we have to find the question to the srcMail
        {
          if(mail->cMsgID && mail->cMsgID == srcMail->cIRTMsgID)
          {
            found = TRUE;
            break;
          }
        }
      }

      // if we still haven`t found the mail we have to scan the other folder aswell
      if(!found && (flist = FO_CreateList()))
      {
        int i;
        int autoloadindex = -1;

        for(i=1; i <= (int)*flist && !found; i++)
        {
          // check if this folder isn't a group and that we haven't scanned
          // it already.
          if(!isGroupFolder(flist[i]) &&
             flist[i] != srcMail->Folder)
          {
            if(flist[i]->LoadedMode != LM_VALID)
            {
              if(autoloadindex == -1)
              {
                if(askLoadAllFolder)
                {
                  // if we are going to ask for loading all folders we do it now
                  if(MUI_Request(G->App, readWindow, 0,
                                 GetStr(MSG_MA_ConfirmReq),
                                 GetStr(MSG_YesNoReq),
                                 GetStr(MSG_RE_FOLLOWTHREAD)))
                  {
                    autoloadindex = 1;
                  }
                  else autoloadindex = 0;
                }
                else autoloadindex = 0;
              }

              // we have to check again and perhaps load the index or continue
              if(autoloadindex == 1)
              {
                if(!MA_GetIndex(flist[i])) continue;
              }
              else continue;
            }

            // if we end up here we have a valid index and can scan the folder
            for(mail = flist[i]->Messages; mail; mail = mail->Next)
            {
              if(nextThread) // find the answer to the srcMail
              {
                if(mail->cIRTMsgID && mail->cIRTMsgID == srcMail->cMsgID)
                {
                  found = TRUE;
                  break;
                }
              }
              else // else we have to find the question to the srcMail
              {
                if(mail->cMsgID && mail->cMsgID == srcMail->cIRTMsgID)
                {
                  found = TRUE;
                  break;
                }
              }
            }
          }
        }
      }
   }

   return mail;
}

///
/// RE_SendMDN
//  Creates a message disposition notification
static void RE_SendMDN(enum MDNType type, struct Mail *mail, struct Person *recipient, BOOL sendnow)
{
   static const char *MDNMessage[5] =
   {
      "The message written on %s (UTC) to %s with subject \"%s\" has been displayed. This is no guarantee that the content has been read or understood.\n",
      "The message written on %s (UTC) to %s with subject \"%s\" has been sent somewhere %s, without being displayed to the user. The user may or may not see the message later.\n",
      "The message written on %s (UTC) to %s with subject \"%s\" has been processed %s, without being displayed to the user. The user may or may not see the message later.\n",
      "The message written on %s (UTC) to %s with subject \"%s\" has been deleted %s. The recipient may or may not have seen the message. The recipient may \"undelete\" the message at a later time and read the message.\n",
      "%s doesn't wish to inform you about the disposition of your message written on %s with subject \"%s\".\n"
   };
   struct WritePart *p1 = NewPart(2), *p2, *p3;
   struct TempFile *tf1, *tf2, *tf3;
   char buf[SIZE_LINE], disp[SIZE_DEFAULT];
   struct Compose comp;

   ENTER();
   SHOWVALUE(DBF_MAIL, sendnow);

   if ((tf1 = OpenTempFile("w")))
   {
      char date[64];
      char *rcpt = BuildAddrName2(&mail->To);
      char *subj = mail->Subject;
      const char *mode;

      DateStamp2String(date, sizeof(date), &mail->Date, DSS_DATETIME, TZC_NONE);

      p1->Filename = tf1->Filename;
      mode = isAutoActMDN(type) ? "automatically" : "in response to a user command";

      snprintf(disp, sizeof(disp), "%s%s", isAutoActMDN(type) ? "automatic-action/" : "manual-action/",
                                           isAutoSendMDN(type) ? "MDN-sent-automatically; " : "MDN-sent-manually; ");

      switch(type & MDN_TYPEMASK)
      {
         case MDN_READ: strlcat(disp, "displayed", sizeof(disp));  fprintf(tf1->FP, MDNMessage[0], date, rcpt, subj); break;
         case MDN_DISP: strlcat(disp, "dispatched", sizeof(disp)); fprintf(tf1->FP, MDNMessage[1], date, rcpt, subj, mode); break;
         case MDN_PROC: strlcat(disp, "processed", sizeof(disp));  fprintf(tf1->FP, MDNMessage[2], date, rcpt, subj, mode); break;
         case MDN_DELE: strlcat(disp, "deleted", sizeof(disp));    fprintf(tf1->FP, MDNMessage[3], date, rcpt, subj, mode); break;
         case MDN_DENY: strlcat(disp, "denied", sizeof(disp));     fprintf(tf1->FP, MDNMessage[4], rcpt, date, subj); break;
      }
      fclose(tf1->FP); tf1->FP = NULL;
      SimpleWordWrap(tf1->Filename, 72);
      p2 = p1->Next = NewPart(2);

      if((tf2 = OpenTempFile("w")))
      {
         char mfile[SIZE_MFILE];
         struct Folder *outfolder = FO_GetFolderByType(FT_OUTGOING, NULL);
         struct ExtendedMail *email = MA_ExamineMail(mail->Folder, mail->MailFile, TRUE);

         if(email)
         {
            p2->ContentType = "message/disposition-notification";
            p2->Filename = tf2->Filename;
            snprintf(buf, sizeof(buf), "%s (%s)", C->SMTP_Domain, yamversion);
            EmitHeader(tf2->FP, "Reporting-UA", buf);
            if (*email->OriginalRcpt.Address)
            {
              snprintf(buf, sizeof(buf), "rfc822;%s", BuildAddrName2(&email->OriginalRcpt));
              EmitHeader(tf2->FP, "Original-Recipient", buf);
            }
            snprintf(buf, sizeof(buf), "rfc822;%s", BuildAddrName(C->EmailAddress, C->RealName));
            EmitHeader(tf2->FP, "Final-Recipient", buf);
            EmitHeader(tf2->FP, "Original-Message-ID", email->MsgID);
            EmitHeader(tf2->FP, "Disposition", disp);
            fclose(tf2->FP);  tf2->FP = NULL;
            p3 = p2->Next = NewPart(2);

            MA_FreeEMailStruct(email);

            if ((tf3 = OpenTempFile("w")))
            {
              char fullfile[SIZE_PATHFILE];
              FILE *fh;
              p3->ContentType = "text/rfc822-headers";
              p3->Filename = tf3->Filename;
              if (StartUnpack(GetMailFile(NULL, mail->Folder, mail), fullfile, mail->Folder))
              {
                if ((fh = fopen(fullfile, "r")))
                {
                  while (fgets(buf, SIZE_LINE, fh)) if (*buf == '\n') break; else fputs(buf, tf3->FP);
                  fclose(fh);
                }
                FinishUnpack(fullfile);
              }
              fclose(tf3->FP);
              tf3->FP = NULL;
              memset(&comp, 0, sizeof(struct Compose));
              comp.MailTo = StrBufCpy(comp.MailTo, BuildAddrName2(recipient));
              comp.Subject = "Disposition Notification";
              comp.ReportType = 1;
              comp.FirstPart = p1;

              if((comp.FH = fopen(MA_NewMailFile(outfolder, mfile), "w")))
              {
                struct Mail *mlist[3];
                mlist[0] = (struct Mail *)1; mlist[2] = NULL;
                WriteOutMessage(&comp);
                fclose(comp.FH);

                if((email = MA_ExamineMail(outfolder, mfile, TRUE)))
                {
                  mlist[2] = AddMailToList(&email->Mail, outfolder);
                  setStatusToQueued(mlist[2]);
                  MA_FreeEMailStruct(email);
                }

                if(sendnow && mlist[2] && !G->TR)
                  TR_ProcessSEND(mlist);

                // refresh the folder statistics
                DisplayStatistics(outfolder, TRUE);
              }
              else
                ER_NewError(GetStr(MSG_ER_CreateMailError));

              FreeStrBuf(comp.MailTo);
              CloseTempFile(tf3);
            }
         }
         CloseTempFile(tf2);
      }
      CloseTempFile(tf1);
   }
   FreePartsList(p1);

   LEAVE();
}
///
/// RE_DoMDN
//  Handles message disposition requests
BOOL RE_DoMDN(enum MDNType type, struct Mail *mail, BOOL multi)
{
   BOOL ignoreall = FALSE;
   int MDNmode;

   switch(type)
   {
      case MDN_READ: MDNmode = C->MDN_Display; break;
      case MDN_PROC:
      case MDN_DISP: MDNmode = C->MDN_Process; break;
      case MDN_DELE: MDNmode = C->MDN_Delete; break;

      default:       MDNmode = C->MDN_Filter; break;
   }

   if(MDNmode)
   {
      struct ExtendedMail *email = MA_ExamineMail(mail->Folder, mail->MailFile, TRUE);

      if(email)
      {
        if(*email->ReceiptTo.Address)
        {
          char buttons[SIZE_DEFAULT*2];
          BOOL isonline = TR_IsOnline();
          BOOL sendnow = C->SendMDNAtOnce && isonline;

          switch(MDNmode)
          {
            case 1:
              type = MDN_DENY|MDN_AUTOSEND|MDN_AUTOACT;
            break;

            case 2:
              sendnow = FALSE;

              snprintf(buttons, sizeof(buttons), "%s%s%s%s", GetStr(MSG_RE_MDNGads1),
                                                             isonline ? GetStr(MSG_RE_MDNGads2) : "",
                                                             GetStr(MSG_RE_MDNGads3),
                                                             multi ? GetStr(MSG_RE_MDNGads4) : "");

              switch (MUI_Request(G->App, G->MA->GUI.WI, 0, GetStr(MSG_MA_ConfirmReq), buttons, GetStr(MSG_RE_MDNReq)))
              {
                case 0: ignoreall = TRUE;
                case 5: type = MDN_IGNORE; break;
                case 3: sendnow = TRUE;
                case 1: break;
                case 4: sendnow = TRUE;
                case 2: type = MDN_DENY; break;
              }
            break;

            case 3:
              if(type != MDN_IGNORE)
                SET_FLAG(type, MDN_AUTOSEND);
            break;
          }

          if(type != MDN_IGNORE)
            RE_SendMDN(type, mail, &email->ReceiptTo, sendnow);
        }

        MA_FreeEMailStruct(email);
      }
   }

   return ignoreall;
}
///
/// RE_SuggestName
//  Suggests a file name based on the message subject
static char *RE_SuggestName(struct Mail *mail)
{
   static char name[SIZE_FILE];
   char *ptr = mail->Subject;
   int i = 0;
   unsigned char tc;

   memset(name, 0, SIZE_FILE);
   while (*ptr && i < 26)
   {
      tc = *ptr++;

      if ((tc <= 32) || (tc > 0x80 && tc < 0xA0) || (tc == ':') || (tc == '/'))
      {
         name[i++] = '_';
      }
      else name[i++] = tc;
   }

   return name;
}

///
/// RE_Export
//  Saves message or attachments to disk
BOOL RE_Export(struct ReadMailData *rmData, const char *source,
               const char *dest, const char *name, int nr, BOOL force, BOOL overwrite, const char *ctype)
{
  char buffer[SIZE_PATHFILE];
  char buffer2[SIZE_FILE+SIZE_DEFAULT];
  Object *win = rmData->readWindow ? rmData->readWindow : G->MA->GUI.WI;
  struct Mail *mail = rmData->mail;

  ENTER();

  if(*dest == '\0')
  {
    struct FileReqCache *frc;

    if(*name != '\0')
    {
      strlcpy(buffer2, name, sizeof(buffer2));
    }
    else if(nr)
    {
      char ext[SIZE_FILE];

      // we have to get the file extension of our source file and use it
      // in our destination file as well
      stcgfe(ext, source);

      name = RE_SuggestName(mail);
      snprintf(buffer2, sizeof(buffer2), "%s-%d.%s", name[0] != '\0' ? name : mail->MailFile, nr, ext[0] != '\0' ? ext : "tmp");
    }
    else
    {
      name = RE_SuggestName(mail);
      snprintf(buffer2, sizeof(buffer2), "%s.msg", name[0] != '\0' ? name : mail->MailFile);
    }

    if(force)
      strmfp(buffer, C->DetachDir, buffer2);
    else if((frc = ReqFile(ASL_DETACH, win, GetStr(MSG_RE_SaveMessage), REQF_SAVEMODE, C->DetachDir, buffer2)))
      strmfp(buffer, frc->drawer, frc->file);
    else
    {
      RETURN(FALSE);
      return FALSE;
    }

    dest = buffer;
  }

  if(FileExists(dest) && !overwrite)
  {
    if(!MUI_Request(G->App, win, 0, GetStr(MSG_MA_ConfirmReq), GetStr(MSG_OkayCancelReq), GetStr(MSG_RE_Overwrite), FilePart(dest)))
    {
      RETURN(FALSE);
      return FALSE;
    }
  }

  if(!CopyFile(dest, 0, source, 0))
  {
    ER_NewError(GetStr(MSG_ER_CantCreateFile), dest);

    RETURN(FALSE);
    return FALSE;
  }
  SetComment(dest, BuildAddrName2(&mail->From));

  if(!stricmp(ctype, IntMimeTypeArray[MT_AP_AEXE].ContentType))
    SetProtection(dest, 0);
  else if(!stricmp(ctype, IntMimeTypeArray[MT_AP_SCRIPT].ContentType))
    SetProtection(dest, FIBF_SCRIPT);

  AppendLogVerbose(80, GetStr(MSG_LOG_SavingAtt), dest, mail->MailFile, FolderName(mail->Folder));

  RETURN(TRUE);
  return TRUE;
}
///
/// RE_PrintFile
//  Prints a file. Currently it is just dumped to PRT:
void RE_PrintFile(char *filename)
{
  if(C->PrinterCheck && !CheckPrinter())
    return;

  switch(C->PrintMethod)
  {
    case PRINTMETHOD_RAW :
      // continue

    default:
      CopyFile("PRT:", 0, filename, 0);
    break;
  }
}

///
/// RE_DisplayMIME
//  Displays a message part (attachment) using a MIME viewer
void RE_DisplayMIME(char *fname, const char *ctype)
{
  struct MinNode *curNode;
  struct MimeTypeNode *mt = NULL;
  BOOL triedToIdentify = FALSE;

  ENTER();

  // in case no content-type was specified, or it is empty we try to be
  // somewhat intelligent, by deeper analyzing the file and its content
  // first.
  if(ctype == NULL || ctype[0] == '\0')
  {
    D(DBF_MIME, "no content-type specified, analyzing '%s'", fname);

    if((ctype = IdentifyFile(fname)))
      D(DBF_MIME, "identified file as '%s'", ctype);

    triedToIdentify = TRUE;
  }

  D(DBF_MIME, "trying to display file '%s' of content-type '%s'", fname, ctype);

  // we first browse through the whole mimeTypeList and try to find
  // out if the content-type spec in one of the user-defined
  // MIME types matches or not.
  if(ctype != NULL)
  {
    for(curNode = C->mimeTypeList.mlh_Head; curNode->mln_Succ; curNode = curNode->mln_Succ)
    {
      struct MimeTypeNode *curType = (struct MimeTypeNode *)curNode;

      if(MatchNoCase(ctype, curType->ContentType))
      {
        mt = curType;
        break;
      }
    }
  }

  // if the MIME part is an rfc822 conform email attachment we
  // try to open it as a virtual mail in another read window.
  if(!mt && ctype != NULL && !stricmp(ctype, "message/rfc822"))
  {
    struct TempFile *tf;

    D(DBF_MIME, "identified content-type as 'message/rfc822'");

    if((tf = OpenTempFile(NULL)))
    {
      struct ExtendedMail *email;

      // copy the contents of our message file into the
      // temporary file.
      if(CopyFile(tf->Filename, NULL, fname, NULL) &&
         (email = MA_ExamineMail(NULL, (char *)FilePart(tf->Filename), TRUE)))
      {
        struct Mail *mail;
        struct ReadMailData *rmData;

        mail = calloc(1, sizeof(struct Mail));
        if(!mail)
        {
          LEAVE();
          return;
        }

        memcpy(mail, &(email->Mail), sizeof(struct Mail));
        mail->Next      = NULL;
        mail->Reference = NULL;
        mail->Folder    = NULL;
        mail->sflags    = SFLAG_READ; // this sets the mail as OLD
        SET_FLAG(mail->mflags, MFLAG_NOFOLDER);

        MA_FreeEMailStruct(email);

        // create the read read window now
        if((rmData = CreateReadWindow(TRUE)))
        {
          rmData->tempFile = tf;

          // make sure it is opened correctly and then read in a mail
          if(SafeOpenWindow(rmData->readWindow) == FALSE ||
             DoMethod(rmData->readWindow, MUIM_ReadWindow_ReadMail, mail) == FALSE)
          {
            // on any error we make sure to delete the read window
            // immediatly again.
            CleanupReadMailData(rmData, TRUE);
          }
        }
        else
        {
          CloseTempFile(tf);
          free(mail);
        }
      }
      else
        CloseTempFile(tf);
    }
  }
  else
  {
    static char command[SIZE_COMMAND+SIZE_PATHFILE];
    char *fileptr;
    char *cmdPtr;

    // if we still didn't found the correct mime type or the command line of the
    // current mime type is empty we use the default mime viewer specified in
    // the YAM configuration.
    if(!mt || mt->Command[0] == '\0')
    {
      cmdPtr = C->DefaultMimeViewer;

      // if we haven't tried to identify the file
      // via IdentifyFile() yet, we try it here
      if(triedToIdentify == FALSE)
      {
        D(DBF_MIME, "haven't found a user action, trying to identifying via IdentifyFile()");

        if((ctype = IdentifyFile(fname)))
        {
          D(DBF_MIME, "identified file as '%s'", ctype);

          for(curNode = C->mimeTypeList.mlh_Head; curNode->mln_Succ; curNode = curNode->mln_Succ)
          {
            struct MimeTypeNode *curType = (struct MimeTypeNode *)curNode;

            if(MatchNoCase(ctype, curType->ContentType))
            {
              if(curType->Command[0] != '\0')
                cmdPtr = curType->Command;

              break;
            }
          }
        }
      }
    }
    else
      cmdPtr = mt->Command;

    D(DBF_MIME, "compiling command sequence of '%s'", cmdPtr);

    // now we have to generate a real commandstring and make sure that
    // the %s is covered with "" or otherwise we will run into trouble with
    // the execute and pathes that have whitespaces.
    if((fileptr = strstr(cmdPtr, "%s")))
    {
      char *startPtr = fileptr;
      char *endPtr = fileptr;

      // lets see if the %s is surrounded by some "" and find the beginning where
      // we should place the quotation marks

      // we first move back until we find whitespace or a quotation mark
      do
      {
        if(*startPtr == ' ' || startPtr == &cmdPtr[0])
        {
          startPtr++;
          break;
        }
        else if(*startPtr == '"')
        {
          startPtr = NULL;
          break;
        }

      }
      while(startPtr--);

      if(startPtr)
      {
        // then we search for the end where we should put the closing quotation mark
        do
        {
          if(*endPtr == ' ' || *endPtr == '\0') break;
          else if(*endPtr == '"')
          {
            endPtr = NULL;
            break;
          }

        }
        while(endPtr++);
      }

      // if we found the start and endPtr we can place the quotation marks there
      // by copying the whole string in a buffer.
      if(startPtr && endPtr)
      {
        char realcmd[SIZE_COMMAND];
        int i,j;

        for(i=0,j=0; ;i++,j++)
        {
          // if this is the start or end we place the first quotation mark
          if(&cmdPtr[j] == startPtr || &cmdPtr[j] == endPtr)
            realcmd[i++] = '"';

          realcmd[i] = cmdPtr[j];

          if(cmdPtr[j] == '\0')
            break;
        }

        snprintf(command, sizeof(command), realcmd, GetRealPath(fname));
      }
      else
        snprintf(command, sizeof(command), cmdPtr, GetRealPath(fname));

      D(DBF_MIME, "executing '%s'", command);

      ExecuteCommand(command, TRUE, OUT_NIL);
    }
    else
      ExecuteCommand(cmdPtr, TRUE, OUT_NIL);
  }

  LEAVE();
}
///
/// RE_SaveAll
//  Saves all attachments to disk
void RE_SaveAll(struct ReadMailData *rmData, char *path)
{
  char *dest;

  ENTER();

  if((dest = calloc(1, strlen(path)+SIZE_DEFAULT+1)))
  {
    struct Part *part;
    char fname[SIZE_DEFAULT];

    for(part = rmData->firstPart->Next; part; part = part->Next)
    {
      // we skip the part which is considered the letterPart
      if(part->Nr != rmData->letterPartNum)
      {
        if(*part->Name)
          strlcpy(fname, part->Name, sizeof(fname));
        else
          snprintf(fname, sizeof(fname), "%s-%d", rmData->mail->MailFile, part->Nr);

        strmfp(dest, path, fname);

        RE_DecodePart(part);
        RE_Export(rmData, part->Filename, dest, part->Name, part->Nr, FALSE, FALSE, part->ContentType);
      }
    }

    free(dest);
  }

  LEAVE();
}
///
/// RE_GetAddressFromLog
//  Finds e-mail address in PGP output
static BOOL RE_GetAddressFromLog(char *buf, char *address)
{
  if((buf = strchr(buf, '"')))
  {
    strlcpy(address, ++buf, SIZE_ADDRESS);
    if((buf = strchr(address, '"')))
      *buf = '\0';

    return TRUE;
  }

  return FALSE;
}
///
/// RE_GetSigFromLog
//  Interprets logfile created from the PGP signature check
void RE_GetSigFromLog(struct ReadMailData *rmData, char *decrFor)
{
  BOOL sigDone = FALSE;
  BOOL decrFail = FALSE;
  FILE *fh;
  char buffer[SIZE_LARGE];

  if((fh = fopen(PGPLOGFILE, "r")))
  {
    while (GetLine(fh, buffer, SIZE_LARGE))
    {
      if(!decrFail && decrFor && G->PGPVersion == 5)
      {
        if(!strnicmp(buffer, "cannot decrypt", 14))
        {
          *decrFor = '\0';
          GetLine(fh, buffer, SIZE_LARGE);
          GetLine(fh, buffer, SIZE_LARGE);
          RE_GetAddressFromLog(buffer, decrFor);
          decrFail = TRUE;
        }
      }

      if(!sigDone)
      {
        if(!strnicmp(buffer, "good signature", 14))
          sigDone = TRUE;
        else if(!strnicmp(buffer, "bad signature", 13) || stristr(buffer, "unknown keyid"))
        {
          SET_FLAG(rmData->signedFlags, PGPS_BADSIG);
          sigDone = TRUE;
        }

        if(sigDone)
        {
          if(G->PGPVersion == 5)
          {
            GetLine(fh, buffer, SIZE_LARGE);
            GetLine(fh, buffer, SIZE_LARGE);
          }

          if(RE_GetAddressFromLog(buffer, rmData->sigAuthor))
            SET_FLAG(rmData->signedFlags, PGPS_ADDRESS);

          SET_FLAG(rmData->signedFlags, PGPS_CHECKED);

          break;
        }
      }
    }
    fclose(fh);

    if(sigDone || (decrFor && !decrFail))
      DeleteFile(PGPLOGFILE);
  }
}
///

/*** MIME ***/
/// ExtractNextParam()
// extracts the name and value of the next upcoming content paramater in string s
// returns the end of string s in case we are finished
static char *ExtractNextParam(char *s, char **name, char **value)
{
  char *p;
  char *u;

  ENTER();

  // skip all leading spaces and return pointer
  // to first real char.
  p = TrimStart(s);

  // extract the paramater name first
  if((s = u = strchr(p, '=')) && u > p)
  {
    char *t;
    int nameLen;

    // skip trailing spaces as well
    while(u > p && *--u && isspace(*u));

    // get the length of the parameter name
    nameLen = u-p+1;

    // allocate enough memory to put in the name
    if((*name = t = malloc(nameLen+1)))
    {
      // copy the parameter name char by char
      // while converting it to lowercase
      while(p <= u)
      {
        *t++ = tolower((int)*p++);
      }

      *t = '\0'; // NUL termination

      D(DBF_MAIL, "name='%s' %d", *name, nameLen);

      // now we go and extract the parameter value, actually
      // taking respect of quoted string and such
      s = TrimStart(++s);
      if(*s)
      {
        BOOL quoted = (*s == '"');
        int valueLen;

        if(quoted)
          s++;

        // we first calculate the value length for
        // allocating the buffer later on
        if((p = strchr(s, ';')))
        {
          valueLen = p-s;
          p++;
        }
        else
        {
          valueLen = strlen(s);
          p = s+valueLen;
        }

        // allocate enough memory to put in the value
        if((*value = t = malloc(valueLen+1)))
        {
          while(valueLen--)
          {
            if(quoted)
            {
              if(*s == '\\')
                s++;
              else if(*s == '"')
                break;
            }

            *t++ = *s++;
          }

          *t = '\0';

          // skip trailing spaces as well if the value
          // wasn't quoted
          if(quoted == FALSE)
          {
            while(t > *value && *--t && isspace(*t))
              *t = '\0';
          }

          D(DBF_MAIL, "value='%s' %d", *value, valueLen);

          RETURN(p);
          return p;
        }
      }

      free(*name);
    }
  }

  *name = NULL;
  *value = NULL;

  RETURN(NULL);
  return NULL;
}

///
/// ParamEnd
//  Finds next parameter in header field
static char *ParamEnd(char *s)
{
  BOOL inquotes = FALSE;

  while(*s)
  {
    if(inquotes)
    {
      if(*s == '"')
        inquotes = FALSE;
      else if(*s == '\\')
        ++s;
    }
    else if(*s == ';')
      return(s);
    else if(*s == '"')
      inquotes = TRUE;

    ++s;
  }

  return NULL;
}
///
/// Cleanse
//  Removes trailing and leading spaces and converts string to lower case
static char *Cleanse(char *s)
{
  char *tmp;

  // skip all leading spaces and return pointer
  // to first real char.
  s = TrimStart(s);

  // convert all chars to lowercase no matter what
  for(tmp=s; *tmp; ++tmp)
    *tmp = tolower((int)*tmp);

  // now we walk back from the end of the string
  // and strip the trailing spaces.
  while(tmp > s && *--tmp && isspace(*tmp))
    *tmp = '\0';

  return s;
}

///
/// RE_ParseContentParameters
//  Parses parameters of Content-Type header field
enum parameterType { PT_CONTENTTYPE, PT_CONTENTDISPOSITION };

static void RE_ParseContentParameters(char *str, struct Part *rp, enum parameterType pType)
{
  char *p = str;
  char *s;
  int size = 0;

  ENTER();

  // scan for the real size of the content-type: value without the
  // corresponding parameters.
  while(*p)
  {
    if(isspace(*p) || *p == ';')
      break;
    else
      size++;

    p++;
  }

  // now we scan through the contentType spec and strip all spaces
  // in between until we reach a ";" which signals a next parameter.
  // we also make sure we just allocate memory for the content-type
  // as our ParseContentParameters() function will allocate the rest.
  if((s = malloc(size+1)))
  {
    char *q = s;
    p=str;

    while(*p)
    {
      if(isspace(*p) || *p == ';')
        break;
      else
        *q++ = *p;

      p++;
    }

    *q = '\0';
  }

  // depending on the parameterType we
  // have to use different source string
  switch(pType)
  {
    case PT_CONTENTTYPE:
    {
      if(rp->ContentType)
        free(rp->ContentType);

      rp->ContentType = s;
    }
    break;

    case PT_CONTENTDISPOSITION:
    {
      if(rp->ContentDisposition)
        free(rp->ContentDisposition);

      rp->ContentDisposition = s;
    }
    break;
  }

  // if we have additional content parameters we go and
  // try to separate them accordingly.
  if(*p == ';' || isspace(*p))
  {
    char *next = ++p;
    char *attribute;
    char *value;

    // now we walk through our string by extracting each
    // content parameter/value combo in a while loop.
    while(*next != '\0' && (next = ExtractNextParam(next, &attribute, &value)))
    {
      if(attribute && value)
      {
        // depending on the parameterType we
        // have to parse different parameters
        switch(pType)
        {
          case PT_CONTENTTYPE:
          {
            if(!strncmp(attribute, "name", 4))
            {
              // we try to find out which encoding the
              // parameter is actually using by checking for
              // an asterisk sign (see: RFC 2231)
              if(attribute[4] == '*')
              {
                static struct codeset *nameCodeset = NULL;
                rfc2231_decode(&attribute[5], value, &rp->CParName, &nameCodeset);
              }
              else
              {
                // otherwise we keep the string as is, as the
                // MA_ReadHeader() function might already converted
                // an eventually existing RFC2047 encoding even if
                // that is normally just reserved for special header lines.
                // However, even modern mail clients seem to still encode
                // even MIME parameters with rfc2047.
                rp->CParName = value;
              }
            }
            else if(!strncmp(attribute, "description", 11))
            {
              // we try to find out which encoding the
              // parameter is actually using by checking for
              // an asterisk sign (see: RFC 2231)
              if(attribute[11] == '*')
              {
                static struct codeset *descCodeset = NULL;
                rfc2231_decode(&attribute[12], value, &rp->CParDesc, &descCodeset);
              }
              else
              {
                // otherwise we keep the string as is, as the
                // MA_ReadHeader() function might already converted
                // an eventually existing RFC2047 encoding even if
                // that is normally just reserved for special header lines.
                // However, even modern mail clients seem to still encode
                // even MIME parameters with rfc2047.
                rp->CParDesc = value;
              }
            }
            else if(!strcmp(attribute, "boundary"))
              rp->CParBndr = value;
            else if(!strcmp(attribute, "protocol"))
              rp->CParProt = value;
            else if(!strcmp(attribute, "report-type"))
              rp->CParRType = value;
            else if(!strcmp(attribute, "charset"))
              rp->CParCSet = value;
            else
              free(value);
          }
          break;

          case PT_CONTENTDISPOSITION:
          {
            if(!strncmp(attribute, "filename", 8))
            {
              // we try to find out which encoding the
              // parameter is actually using by checking for
              // an asterisk sign (see: RFC 2231)
              if(attribute[8] == '*')
              {
                static struct codeset *fileNameCodeset = NULL;
                rfc2231_decode(&attribute[9], value, &rp->CParFileName, &fileNameCodeset);
              }
              else
              {
                // otherwise we keep the string as is, as the
                // MA_ReadHeader() function might already converted
                // an eventually existing RFC2047 encoding even if
                // that is normally just reserved for special header lines.
                // However, even modern mail clients seem to still encode
                // even MIME parameters with rfc2047.
                rp->CParFileName = value;
              }
            }
            else
              free(value);
          }
          break;
        }

        free(attribute);
      }
      else
      {
        E(DBF_MIME, "couldn't extract a full parameter (%lx/%lx)", attribute, value);

        if(attribute)
          free(attribute);

        if(value)
          free(value);
      }
    }
  }

  LEAVE();
}
///
/// RE_ScanHeader
//  Parses the header of the message or of a message part
static BOOL RE_ScanHeader(struct Part *rp, FILE *in, FILE *out, int mode)
{
  struct MinNode *curNode;

  ENTER();

  // check if we already have a headerList and if so we clean it first
  if(rp->headerList)
    FreeHeaderList(rp->headerList);
  else
  {
    // we do not have any headerList yet so lets allocate a new one
    if((rp->headerList = calloc(1, sizeof(struct MinList))) == NULL)
    {
      RETURN(FALSE);
      return FALSE;
    }
  }

  // we read in the headers from our mail file
  if(!MA_ReadHeader(in, rp->headerList))
  {
    if(mode == 0)
    {
      if(hasFlag(rp->rmData->parseFlags, PM_QUIET) == FALSE)
        ER_NewError(GetStr(MSG_ER_MIMEError));
    }
    else if(mode == 1)
    {
      if(hasFlag(rp->rmData->parseFlags, PM_QUIET) == FALSE)
        ER_NewError(GetStr(MSG_ER_MultipartEOF));
    }

    rp->HasHeaders = FALSE;

    RETURN(FALSE);
    return FALSE;
  }
  else
    rp->HasHeaders = TRUE;

  // Now we process the read header to set all flags accordingly
  for(curNode = rp->headerList->mlh_Head; curNode->mln_Succ; curNode = curNode->mln_Succ)
  {
    struct HeaderNode *hdrNode = (struct HeaderNode *)curNode;
    char *field = hdrNode->name;
    char *value = hdrNode->content;
    int headerLen = strlen(field)+strlen(value)+2;

    // check the real one-line headerLen
    if(headerLen > rp->MaxHeaderLen)
      rp->MaxHeaderLen = headerLen;

    // if we have a fileoutput pointer lets write out the header immediatly
    if(out)
      fprintf(out, "%s: %s\n", field, value);

    if(!stricmp(field, "content-type"))
    {
      // we check whether we have a content-type value or not, because otherwise
      // we have to keep the default "text/plain" content-type value the
      // OpenNewPart() function sets
      if(value[0] != '\0')
        RE_ParseContentParameters(value, rp, PT_CONTENTTYPE);
      else
        W(DBF_MAIL, "Empty 'Content-Type' headerline found.. using default '%s'.", rp->ContentType);

      // check the alternative part status
      // and try to find out if this is the main alternative part
      // which we might show later
      if(rp->isAltPart && rp->Parent)
      {
        if(stricmp(rp->ContentType, "text/plain") == 0 ||
           rp->Parent->MainAltPart == NULL)
        {
          D(DBF_MAIL, "setting new main alternative part in parent #%d [%lx] to #%d [%lx]", rp->Parent->Nr, rp->Parent, rp->Nr, rp);

          rp->Parent->MainAltPart = rp;
        }
      }
    }
    else if(!stricmp(field, "content-transfer-encoding"))
    {
      char *p;
      char buf[SIZE_DEFAULT];

      strlcpy(p = buf, value, sizeof(buf));
      TrimEnd(p);

      // As the content-transfer-encoding field is mostly used in
      // attachment MIME fields, we first check for common attachement encodings
      if(!strnicmp(p, "base64", 6))
        rp->EncodingCode = ENC_B64;
      else if(!strnicmp(p, "quoted-printable", 16))
        rp->EncodingCode = ENC_QP;
      else if(!strnicmp(p, "8bit", 4) || !strnicmp(p, "8-bit", 5))
        rp->EncodingCode = ENC_8BIT;
      else if(!strnicmp(p, "7bit", 4) || !strnicmp(p, "7-bit", 5) ||
              !strnicmp(p, "plain", 5) || !strnicmp(p, "none", 4))
      {
        rp->EncodingCode = ENC_NONE;
      }
      else if(!strnicmp(p, "x-uue", 5))
        rp->EncodingCode = ENC_UUE;
      else if(!strnicmp(p, "binary", 6))
        rp->EncodingCode = ENC_BIN;
      else
      {
        ER_NewError(GetStr(MSG_ER_UnknownEnc), p);

        // set the default to ENC_NONE
        rp->EncodingCode = ENC_NONE;
      }
    }
    else if(!stricmp(field, "content-description"))
    {
      // we just copy the value here as the initial rfc2047 decoding
      // was done in MA_ReadHeader() already.
      strlcpy(rp->Description, value, sizeof(rp->Description));
    }
    else if(!stricmp(field, "content-disposition"))
    {
      RE_ParseContentParameters(value, rp, PT_CONTENTDISPOSITION);
    }
  }

  RETURN(TRUE);
  return TRUE;
}
///
/// RE_ConsumeRestOfPart
//  Processes body of a message part
static BOOL RE_ConsumeRestOfPart(FILE *in, FILE *out, struct codeset *srcCodeset, struct Part *rp, BOOL allowAutoDetect)
{
  char buf[SIZE_LINE];
  int blen = 0;
  BOOL prependNewline = FALSE;

  ENTER();

  if(!in)
  {
    RETURN(FALSE);
    return FALSE;
  }

  if(rp)
    blen = strlen(rp->CParBndr);

  // we process the file line-by-line, analyze it if it is between the boundary
  // do an eventually existing charset translation and write it out again.
  while(fgets(buf, SIZE_LINE, in))
  {
    char *pNewline;

    // search for either a \r or \n and terminate there
    // if found.
    if((pNewline = strpbrk(buf, "\r\n")))
      *pNewline = '\0'; // strip any newline

    // first we check if we reached a MIME boundary yet.
    if(blen > 0 && buf[0] == '-' && buf[1] == '-' && strncmp(buf+2, rp->CParBndr, blen) == 0)
    {
      if(buf[blen+2] == '-' && buf[blen+3] == '-' && buf[blen+4] == '\0')
      {
        RETURN(TRUE);
        return TRUE;
      }
      else
      {
        RETURN(FALSE);
        return FALSE;
      }
    }

    if(out)
    {
      int buflen = strlen(buf);

      // just in case our input buffer is > 0 we go on
      // or otherwise we continue or iteration
      if(buflen > 0)
      {
        // in case the user wants us to detect the correct cyrillic codeset
        // we do it now
        if(C->DetectCyrillic && allowAutoDetect)
        {
          struct codeset *cs = CodesetsFindBest(CSA_Source,         buf,
                                                CSA_SourceLen,      buflen,
                                                CSA_CodesetFamily,  CSV_CodesetFamily_Cyrillic,
                                                TAG_DONE);

          if(cs != NULL && cs != srcCodeset)
            srcCodeset = cs;
        }

        // if this function was invoked with a source Codeset we have to make sure
        // we convert from the supplied source Codeset to our current local codeset with
        // help of the functions codesets.library provides.
        if(srcCodeset)
        {
          // convert from the srcCodeset to the destination one.
          char *str = CodesetsConvertStr(CSA_SourceCodeset, srcCodeset,
                                         CSA_DestCodeset,   G->localCharset,
                                         CSA_Source,        buf,
                                         CSA_SourceLen,     buflen,
                                         TAG_DONE);

          // now write back exactly the same amount of bytes we have read
          // previously
          if(fprintf(out, "%s%s", prependNewline ? "\n" : "", str ? str : buf) <= 0)
          {
            E(DBF_MAIL, "error during write operation!");

            RETURN(FALSE);
            return FALSE;
          }

          if(str)
            CodesetsFreeA(str, NULL);
          else
            W(DBF_MAIL, "couldn't convert str with CodesetsConvertStr()");
        }
        else
        {
          // now write back exactly the same amount of bytes we read previously
          if(fprintf(out, "%s%s", prependNewline ? "\n" : "", buf) <= 0)
          {
            E(DBF_MAIL, "error during write operation!");

            RETURN(FALSE);
            return FALSE;
          }
        }
      }
      else if(prependNewline)
        fputc('\n', out);

      // check if the next iteration should prepend a newline or not.
      if(pNewline)
        prependNewline = TRUE;
      else
        prependNewline = FALSE;
    }
  }

  // if we end up here because of a EOF we have check
  // if there is still something in c and then write it into the out fh.
  if(feof(in))
  {
    // if we still have a prependNewline as TRUE we have to add
    // a single newline
    if(out && prependNewline)
      fputc('\n', out);

    RETURN(TRUE);
    return TRUE;
  }

  RETURN(FALSE);
  return FALSE;
}
///
/// RE_DecodeStream
//  Decodes contents of a part
//  return 0 on error
//  return 1 on success (was decoded)
//  return 2 on success (no decode required, no data written to out)
//  return 3 on success (no decode required, data without headers written to out)
static int RE_DecodeStream(struct Part *rp, FILE *in, FILE *out)
{
  int decodeResult = 0;
  struct codeset *sourceCodeset = NULL;
  struct ReadMailData *rmData = rp->rmData;
  BOOL quietParsing = hasFlag(rmData->parseFlags, PM_QUIET);

  ENTER();

  // now we find out if we should decode charset aware. This means
  // that we make sure that we convert the text in "in" into our
  // local charset or not.
  if(rp->Nr != PART_RAW && rp->Printable && rp->CParCSet != NULL)
  {
    // now we check that the codeset of the mail part really
    // differs from the local one we are currently using
    if(stricmp(rp->CParCSet, strippedCharsetName(G->localCharset)) != 0)
    {
      D(DBF_MAIL, "found Part #%d encoded in charset '%s' which is different than local one.", rp->Nr, rp->CParCSet);

      // try to obtain the source codeset from codesets.library
      // such that we can convert to our local charset accordingly.
      if((sourceCodeset = CodesetsFind(rp->CParCSet,
                                       CSA_CodesetList,       G->codesetsList,
                                       CSA_FallbackToDefault, FALSE,
                                       TAG_DONE)) == NULL)
      {
        W(DBF_MAIL, "the specified codeset wasn't found in codesets.library.");
      }
    }
  }

  // lets check if we got some encoding here and
  // if so we have to decode it immediatly
  switch(rp->EncodingCode)
  {
    // process a base64 decoding.
    case ENC_B64:
    {
      int decoded = base64decode_file(in, out, sourceCodeset, rp->Printable);
      D(DBF_MAIL, "base64 decoded %ld bytes of part %ld.", decoded, rp->Nr);

      if(decoded > 0)
        decodeResult = 1;
      else
      {
        // now we check whether the part we decoded was
        // a printable one, and if so we have to just throw a warning at
        // the user abour the problem and still set the decodeResult to 1
        if(rp->Printable)
        {
          // as this is just a "warning" we can skip the error message
          // in case we parse the message with q PM_QUIET flag
          if(quietParsing == FALSE)
            ER_NewError(GetStr(MSG_ER_B64DECTRUNCTXT), rp->Nr, rmData->readFile);

          decodeResult = 1;
        }
        else
        {
          // if that part was not a printable/viewable one we
          // have to make sure decodeResult is set to 0 to signal
          // the caller that it should not expect that the decoded part
          // is valid
          if(quietParsing == FALSE)
            ER_NewError(GetStr(MSG_ER_B64DECTRUNC), rp->Nr, rmData->readFile);
        }
      }
    }
    break;

    // process a Quoted-Printable decoding
    case ENC_QP:
    {
      long decoded = qpdecode_file(in, out, sourceCodeset);
      D(DBF_MAIL, "quoted-printable decoded %ld chars of part %ld.", decoded, rp->Nr);

      if(decoded >= 0)
        decodeResult = 1;
      else
      {
        switch(decoded)
        {
          case -1:
          {
            if(quietParsing == FALSE)
              ER_NewError(GetStr(MSG_ER_QPDEC_FILEIO), rp->Filename);
          }
          break;

          case -2:
          {
            if(quietParsing == FALSE)
              ER_NewError(GetStr(MSG_ER_QPDEC_UNEXP), rp->Filename);
          }
          break;

          case -3:
          {
            W(DBF_MAIL, "found an undecodeable qp char sequence. Warning the user.");

            if(quietParsing == FALSE)
              ER_NewError(GetStr(MSG_ER_QPDEC_WARN), rp->Filename);

            decodeResult = 1; // allow to save the resulting file
          }
          break;

          case -4:
          {
            W(DBF_MAIL, "found an invalid character during decoding. Warning the user.");

            if(quietParsing == FALSE)
              ER_NewError(GetStr(MSG_ER_QPDEC_CHAR), rp->Filename);

            decodeResult = 1; // allow to save the resulting file
          }
          break;

          default:
          {
            if(quietParsing == FALSE)
              ER_NewError(GetStr(MSG_ER_QPDEC_UNEXP), rp->Filename);
          }
          break;
        }
      }
    }
    break;

    // process UU-Encoded decoding
    case ENC_UUE:
    {
      long decoded = uudecode_file(in, out, sourceCodeset);
      D(DBF_MAIL, "UU decoded %ld chars of part %ld.", decoded, rp->Nr);

      if(decoded >= 0 &&
        RE_ConsumeRestOfPart(in, NULL, NULL, NULL, FALSE))
      {
        decodeResult = 1;
      }
      else
      {
        switch(decoded)
        {
          case -1:
          {
            if(quietParsing == FALSE)
              ER_NewError(GetStr(MSG_ER_UnexpEOFUU));
          }
          break;

          case -2:
          {
            if(quietParsing == FALSE)
              ER_NewError(GetStr(MSG_ER_UUDEC_TAGMISS), rp->Filename, "begin");
          }
          break;

          case -3:
          {
            if(quietParsing == FALSE)
              ER_NewError(GetStr(MSG_ER_InvalidLength), 0);
          }
          break;

          case -4:
          {
            if(quietParsing == FALSE)
              ER_NewError(GetStr(MSG_ER_UUDEC_CHECKSUM), rp->Filename);

            decodeResult = 1; // allow to save the resulting file
          }
          break;

          case -5:
          {
            if(quietParsing == FALSE)
              ER_NewError(GetStr(MSG_ER_UUDEC_CORRUPT), rp->Filename);
          }
          break;

          case -6:
          {
            if(quietParsing == FALSE)
              ER_NewError(GetStr(MSG_ER_UUDEC_TAGMISS), rp->Filename, "end");

            decodeResult = 1; // allow to save the resulting file
          }
          break;

          default:
          {
            if(quietParsing == FALSE)
              ER_NewError(GetStr(MSG_ER_UnexpEOFUU));
          }
        }
      }
    }
    break;

    default:
    {
      if(sourceCodeset || C->DetectCyrillic)
      {
        if(RE_ConsumeRestOfPart(in, out, sourceCodeset, NULL, TRUE))
          decodeResult = 1;
      }
      else if(rp->HasHeaders)
      {
        if(CopyFile(NULL, out, NULL, in))
          decodeResult = 3;
      }
      else
        decodeResult = 2;
    }
  }

  RETURN(decodeResult);
  return decodeResult;
}
///
/// RE_OpenNewPart
//  Adds a new entry to the message part list
static FILE *RE_OpenNewPart(struct ReadMailData *rmData,
                            struct Part **new,
                            struct Part *prev,
                            struct Part *first)
{
  FILE *fp;
  struct Part *newPart;

  ENTER();

  if(((*new) = newPart = calloc(1,sizeof(struct Part))))
  {
    char file[SIZE_FILE];

    if(prev)
    {
      // link in the new Part
      newPart->Prev = prev;
      prev->Next = newPart;
      newPart->Nr = prev->Nr+1;
    }

    if(first && strnicmp(first->ContentType, "multipart", 9) != 0)
    {
      newPart->ContentType = strdup(first->ContentType);
      newPart->CParCSet = first->CParCSet ? strdup(first->CParCSet) : NULL;
      newPart->EncodingCode = first->EncodingCode;
    }
    else
    {
      newPart->ContentType = strdup("text/plain");
      newPart->EncodingCode = ENC_NONE;

      if(first && (first->isAltPart ||
         (first->ContentType[9] != '\0' && strnicmp(&first->ContentType[10], "alternative", 11) == 0)))
      {
        newPart->isAltPart = TRUE;
      }
    }

    // make sure we make the hierarchy clear
    newPart->Parent = first;

    // copy the boundary specification
    newPart->CParBndr = strdup(first ? first->CParBndr : (prev ? prev->CParBndr : ""));

    newPart->rmData = rmData;
    snprintf(file, sizeof(file), "YAMr%08lx-p%d.txt", readMailDataID(rmData), newPart->Nr);
    strmfp(newPart->Filename, C->TempDir, file);

    D(DBF_MAIL, "New Part #%d [%lx]", newPart->Nr, newPart);
    D(DBF_MAIL, "  IsAltPart..: %ld",  newPart->isAltPart);
    D(DBF_MAIL, "  Filename...: [%s]", newPart->Filename);
    D(DBF_MAIL, "  Nextptr....: %lx",  newPart->Next);
    D(DBF_MAIL, "  Prevptr....: %lx",  newPart->Prev);
    D(DBF_MAIL, "  Parentptr..: %lx",  newPart->Parent);
    D(DBF_MAIL, "  MainAltPart: %lx",  newPart->MainAltPart);

    if((fp = fopen(newPart->Filename, "w")))
    {
      RETURN(fp);
      return fp;
    }

    free(newPart);
    *new = NULL;
  }

  E(DBF_MAIL, "Error: Couldn't create a new Part!");

  RETURN(NULL);
  return NULL;
}
///
/// RE_UndoPart
//  Removes an entry from the message part list
static void RE_UndoPart(struct Part *rp)
{
  struct Part *trp = rp;

  ENTER();

  D(DBF_MAIL, "Undoing part #%ld [%lx]", rp->Nr, rp);

  // lets delete the file first so that we can cleanly "undo" the part
  DeleteFile(rp->Filename);

  // we only iterate through our partlist if there is
  // a next item, if not we can simply relink it
  if(trp->Next)
  {
    // if we remove a part from the part list we have to take
    // care of the part index number aswell. So all following
    // parts have to be descreased somehow by one.
    //
    // p2->p3->p4->p5
    do
    {
      // use the next element as the current trp
      trp = trp->Next;

      // decrease the part number aswell
      trp->Nr--;

      // Now we also have to rename the temporary filename also
      Rename(trp->Filename, trp->Prev->Filename);

    }
    while(trp->Next);

    // now go from the end to the start again and copy
    // the filenames strings as we couldn`t do that in the previous
    // loop also
    //
    // p5->p4->p3->p2
    do
    {
      // iterate backwards
      trp = trp->Prev;

      // now copy the filename string
      strlcpy(trp->Next->Filename, trp->Filename, sizeof(trp->Next->Filename));

    }
    while(trp->Prev && trp != rp);
  }

  // relink the partlist
  if(rp->Prev) rp->Prev->Next = rp->Next;
  if(rp->Next) rp->Next->Prev = rp->Prev;

  // free an eventually existing headerList
  if(rp->headerList)
  {
    FreeHeaderList(rp->headerList);
    free(rp->headerList);
  }

  // free some string buffers
  free(rp->ContentType);
  free(rp->ContentDisposition);

  // free all the CPar structue members
  if(rp->CParName)      free(rp->CParName);
  if(rp->CParFileName)  free(rp->CParFileName);
  if(rp->CParBndr)      free(rp->CParBndr);
  if(rp->CParProt)      free(rp->CParProt);
  if(rp->CParDesc)      free(rp->CParDesc);
  if(rp->CParRType)     free(rp->CParRType);
  if(rp->CParCSet)      free(rp->CParCSet);

  // now we check whether the readMailData letterPartNum has to be decreased to
  // point to the correct letterPart number again
  if(rp->rmData)
  {
    struct ReadMailData *rmData = rp->rmData;

    // if the letterPart is that what is was removed from our part list
    // we have to find out which one is the new letterPart indeed
    if(rmData->letterPartNum == rp->Nr)
    {
      rmData->letterPartNum = 0;

      for(trp = rmData->firstPart; trp; trp = trp->Next)
      {
        if(trp->Nr > PART_RAW && trp->Nr >= PART_LETTER &&
           trp->Printable)
        {
          rmData->letterPartNum = trp->Nr;
          break;
        }
      }
    }
    else if(rmData->letterPartNum > rp->Nr)
      rmData->letterPartNum--;

    D(DBF_MAIL, "new letterpart is #%ld", rmData->letterPartNum);
  }

  // now we also have to relink the Parent pointer
  for(trp=rp; trp; trp = trp->Next)
  {
    if(trp->Parent == rp)
      trp->Parent = rp->Parent;
  }

  // relink the MainAltPart pointer as well
  if(rp->Parent && rp->Parent->MainAltPart == NULL)
  {
    D(DBF_MAIL, "setting parent #%d [%lx] MainAltPart to %lx", rp->Parent->Nr, rp->Parent, rp->MainAltPart);

    rp->Parent->MainAltPart = rp->MainAltPart;
  }

  // and last, but not least we free the part
  free(rp);

  LEAVE();
}
///
/// RE_RequiresSpecialHandling
//  Checks if part is PGP signed/encrypted or a MDN
static int RE_RequiresSpecialHandling(struct Part *hrp)
{
  int res = 0;
  ENTER();

  if(hrp->CParRType && !stricmp(hrp->ContentType, "multipart/report") &&
                       !stricmp(hrp->CParRType, "disposition-notification"))
  {
    res = 1;
  }
  else if(hrp->CParProt)
  {
    if(!stricmp(hrp->ContentType, "multipart/signed") &&
       !stricmp(hrp->CParProt, "application/pgp-signature"))
    {
      res = 2;
    }
    else if(!stricmp(hrp->ContentType, "multipart/encrypted") &&
            !stricmp(hrp->CParProt, "application/pgp-encrypted"))
    {
      res = 3;
    }
  }

  RETURN(res);
  return res;
}
///
/// RE_SaveThisPart
//  Decides if the part should be kept in memory
static BOOL RE_SaveThisPart(struct Part *rp)
{
  BOOL result = FALSE;
  short parseFlags = rp->rmData->parseFlags;

  ENTER();

  if(hasFlag(parseFlags, PM_ALL))
    result = TRUE;
  else if(hasFlag(parseFlags, PM_TEXTS) && strnicmp(rp->ContentType, "text", 4) == 0)
    result = TRUE;

  RETURN(result);
  return result;
}
///
/// RE_SetPartInfo
//  Determines size and other information of a message part
static void RE_SetPartInfo(struct Part *rp)
{
   int size = rp->Size = FileSize(rp->Filename);

   // let`s calculate the partsize on a undecoded part, if not a common part
   if(!rp->Decoded && rp->Nr > 0)
   {
      switch (rp->EncodingCode)
      {
        case ENC_UUE: case ENC_B64: rp->Size = (100*size)/136; break;
        case ENC_QP:                rp->Size = (100*size)/106; break;
        default:
          // nothing
        break;
      }
   }

   // if this part hasn`t got any name, we place the CParName as the normal name
   if(!*rp->Name && (rp->CParName || rp->CParFileName))
      strlcpy(rp->Name, rp->CParName ? rp->CParName : rp->CParFileName, sizeof(rp->Name));

   // let`s set if this is a printable (readable part)
   rp->Printable = !strnicmp(rp->ContentType, "text", 4) || rp->Nr == PART_RAW;

   // Now that we have defined that this part is printable we have
   // to check whether our readMailData structure already contains a reference
   // to the actual readable letterPart or not and if not we do make this
   // part the actual letterPart
   if((rp->rmData->letterPartNum < PART_LETTER ||
       (rp->isAltPart == TRUE && rp->Parent != NULL && rp->Parent->MainAltPart == rp)) &&
      rp->Printable &&
      rp->Nr >= PART_LETTER)
   {
     D(DBF_MAIL, "setting part #%ld as LETTERPART", rp->Nr);

     rp->rmData->letterPartNum = rp->Nr;

     SetComment(rp->Filename, GetStr(MSG_RE_Letter));
   }
   else if(rp->Nr == PART_RAW)
     SetComment(rp->Filename, GetStr(MSG_RE_Header));
   else
   {
      // if this is not a printable LETTER part or a RAW part we
      // write another comment
      SetComment(rp->Filename, *rp->Description ? rp->Description : (*rp->Name ? rp->Name : rp->ContentType));
   }
}
///
/// RE_ParseMessage (rec)
//  Parses a complete message
static struct Part *RE_ParseMessage(struct ReadMailData *rmData,
                                    FILE *in,
                                    char *fname,
                                    struct Part *hrp)
{
  ENTER();

  D(DBF_MAIL, "ParseMessage(): %08lx, %08lx, %08lx, %08lx", rmData, in, fname, hrp);

  if(in == NULL && fname)
    in = fopen(fname, "r");

  if(in)
  {
    FILE *out;
    struct Part *rp;

    if(hrp == NULL)
    {
      if((out = RE_OpenNewPart(rmData, &hrp, NULL, NULL)))
      {
        BOOL parse_ok = RE_ScanHeader(hrp, in, out, 0);

        fclose(out);

        if(parse_ok)
          RE_SetPartInfo(hrp);
      }
      else
        ER_NewError(GetStr(MSG_ER_CantCreateTempfile));
    }

    if(hrp)
    {
      if(hrp->CParBndr != NULL && strnicmp(hrp->ContentType, "multipart", 9) == 0)
      {
        BOOL done = RE_ConsumeRestOfPart(in, NULL, NULL, hrp, FALSE);

        rp = hrp;

        while(!done)
        {
          struct Part *prev = rp;
          out = RE_OpenNewPart(rmData, &rp, prev, hrp);

          if(out == NULL) break;

          if(!RE_ScanHeader(rp, in, out, 1))
          {
            fclose(out);
            RE_UndoPart(rp);
            break;
          }

          if(strnicmp(rp->ContentType, "multipart", 9) == 0)
          {
            fclose(out);

            if(RE_ParseMessage(rmData, in, NULL, rp))
            {
              // undo the dummy part
              RE_UndoPart(rp);

              // but consume all rest of the part
              done = RE_ConsumeRestOfPart(in, NULL, NULL, prev, FALSE);
              for(rp = prev; rp->Next; rp = rp->Next)
                ;
            }
          }
          else if (RE_SaveThisPart(rp) || RE_RequiresSpecialHandling(hrp) == 3)
          {
            fputc('\n', out);
            done = RE_ConsumeRestOfPart(in, out, NULL, rp, FALSE);
            fclose(out);
            RE_SetPartInfo(rp);
          }
          else
          {
            fclose(out);
            done = RE_ConsumeRestOfPart(in, NULL, NULL, rp, FALSE);
            RE_UndoPart(rp);
            rp = prev;
          }
        }
      }
      else if((out = RE_OpenNewPart(rmData, &rp, hrp, hrp)))
      {
        if(RE_SaveThisPart(rp) || RE_RequiresSpecialHandling(hrp) == 3)
        {
          RE_ConsumeRestOfPart(in, out, NULL, NULL, FALSE);
          fclose(out);
          RE_SetPartInfo(rp);
        }
        else
        {
          fclose(out);
          RE_UndoPart(rp);
          RE_ConsumeRestOfPart(in, NULL, NULL, NULL, FALSE);
        }
      }
    }

    if(fname)
      fclose(in);
  }

  #if defined(DEBUG)
  if(fname)
  {
    struct Part *rp;

    D(DBF_MAIL, "HeaderPart: [%lx]", hrp);

    for(rp = hrp; rp; rp = rp->Next)
    {
      D(DBF_MAIL, "Part[%lx]:#%ld%s", rp, rp->Nr, rp->Nr == rp->rmData->letterPartNum ? ":LETTERPART" : "");
      D(DBF_MAIL, "  Name.......: [%s]", rp->Name);
      D(DBF_MAIL, "  ContentType: [%s]", rp->ContentType);
      D(DBF_MAIL, "  Boundary...: [%s]", rp->CParBndr ? rp->CParBndr : "NULL");
      D(DBF_MAIL, "  Charset....: [%s]", rp->CParCSet ? rp->CParCSet : "NULL");
      D(DBF_MAIL, "  IsAltPart..: %ld",  rp->isAltPart);
      D(DBF_MAIL, "  Printable..: %ld",  rp->Printable);
      D(DBF_MAIL, "  Encoding...: %ld",  rp->EncodingCode);
      D(DBF_MAIL, "  Filename...: [%s]", rp->Filename);
      D(DBF_MAIL, "  Size.......: %ld",  rp->Size);
      D(DBF_MAIL, "  Nextptr....: %lx",  rp->Next);
      D(DBF_MAIL, "  Prevptr....: %lx",  rp->Prev);
      D(DBF_MAIL, "  Parentptr..: %lx",  rp->Parent);
      D(DBF_MAIL, "  MainAltPart: %lx",  rp->MainAltPart);
      D(DBF_MAIL, "  headerList.: %lx",  rp->headerList);
    }
  }
  #endif

  RETURN(hrp);
  return hrp;
}
///
/// RE_DecodePart
//  Decodes a single message part
BOOL RE_DecodePart(struct Part *rp)
{
  // it only makes sense to go on here if
  // the data wasn`t decoded before.
  if(!rp->Decoded)
  {
    FILE *in, *out;
    char file[SIZE_FILE];
    char buf[SIZE_LINE];
    char ext[SIZE_FILE] = "\0";

    if((in = fopen(rp->Filename, "r")))
    {
      // if this part has some headers, let`s skip them so that
      // we just decode the raw data.
      if(rp->HasHeaders)
      {
        while(GetLine(in, buf, SIZE_LINE))
        {
          // if we find an empty string it is a sign that
          // by starting from the next line the encoded data
          // should follow
          if(*buf == '\0')
            break;
        }

        // we only go on if we are not in an ferror() condition
        // as we shouldn`t have a EOF or real error here.
        if(ferror(in) || feof(in))
        {
          E(DBF_MAIL, "ferror() or feof() while parsing through PartHeader.");
          fclose(in);
          return FALSE;
        }
      }

      // we try to get a proper file extension for our decoded part which we
      // in fact first try to get out of the user's MIME configuration.
      if(rp->Nr != PART_RAW)
      {
        // only if we have a contentType we search through our MIME list
        if(rp->ContentType && rp->ContentType[0] != '\0')
        {
          struct MinNode *curNode;

          for(curNode = C->mimeTypeList.mlh_Head; curNode->mln_Succ; curNode = curNode->mln_Succ)
          {
            struct MimeTypeNode *curType = (struct MimeTypeNode *)curNode;

            if(MatchNoCase(rp->ContentType, curType->ContentType))
            {
              char *s = TrimStart(curType->Extension);
              char *e;

              if((e = strpbrk(s, " |;,")) == NULL)
                e = s+strlen(s);

              D(DBF_MIME, "identified file extension: '%s' %d", s, e-s);

              strlcpy(ext, s, MIN(sizeof(ext), (unsigned int)(e-s+1)));

              break;
            }
          }
        }

        // if we still don't have a valid extension, we try to take it from
        // and eventually existing part name
        if(ext[0] == '\0' && rp->Name[0] != '\0')
        {
          // get the file extension name
          stcgfe(ext, rp->Name);
          if(strlen(ext) > 5)
            ext[0] = '\0'; // if the file extension is longer than 5 chars lets use "tmp"
        }
      }

      // lets generate the destination file name for the decoded part
      snprintf(file, sizeof(file), "YAMm%08lx-p%d.%s", readMailDataID(rp->rmData), rp->Nr, ext[0] != '\0' ? ext : "tmp");
      strmfp(buf, C->TempDir, file);

      D(DBF_MAIL, "decoding '%s' to '%s'", rp->Filename, buf);

      // now open the stream and decode it afterwards.
      if((out = fopen(buf, "w")))
      {
        int decodeResult = RE_DecodeStream(rp, in, out);

        // close the streams
        fclose(out);
        fclose(in);

        // check if we were successfull in decoding the data.
        if(decodeResult > 0)
        {
          // if decodeResult == 2 then no decode was required and we just have to rename
          // the file
          if(decodeResult == 2)
          {
            D(DBF_MAIL, "no decode required. renaming file.");

            DeleteFile(buf); // delete the temporary file again.
            rp->Decoded = Rename(rp->Filename, buf);
          }
          else
          {
            D(DBF_MAIL, "%s", decodeResult == 1 ? "successfully decoded" : "no decode required. did a raw copy");

            DeleteFile(rp->Filename);
            rp->Decoded = TRUE;
          }

          strlcpy(rp->Filename, buf, sizeof(rp->Filename));
          RE_SetPartInfo(rp);
        }
        else
        {
          E(DBF_MAIL, "error during RE_DecodeStream()");
          DeleteFile(buf); // delete the temporary file again.
        }
      }
      else if((out = fopen(buf, "r")))
      {
        // if we couldn`t open that file for writing we check if it exists
        // and if so we use it because it is locked actually and already decoded
        fclose(out);
        fclose(in);
        DeleteFile(rp->Filename);
        strlcpy(rp->Filename, buf, sizeof(rp->Filename));
        rp->Decoded = TRUE;
        RE_SetPartInfo(rp);
      }
      else
        fclose(in);
    }
  }

  return rp->Decoded;
}
///
/// RE_HandleMDNReport
//  Translates a message disposition notification to readable text
static void RE_HandleMDNReport(struct Part *frp)
{
  struct Part *rp[3];
  char file[SIZE_FILE], buf[SIZE_PATHFILE], MDNtype[SIZE_DEFAULT];
  char *msgdesc, *type;
  const char *mode = "";
  int j;
  FILE *out, *fh;

  if((rp[0] = frp->Next) && (rp[1] = rp[0]->Next))
  {
    rp[2] = rp[1]->Next;
    msgdesc = AllocStrBuf(80);
    MDNtype[0] = '\0';

    for(j = 1; j < (rp[2] ? 3 : 2); j++)
    {
      RE_DecodePart(rp[j]);

      if((fh = fopen(rp[j]->Filename, "r")))
      {
        struct MinList *headerList = calloc(1, sizeof(struct MinList));

        if(headerList)
        {
          // read in the header into the headerList
          MA_ReadHeader(fh, headerList);
          fclose(fh);

          if(IsMinListEmpty(headerList) == FALSE)
          {
            struct MinNode *curNode;

            for(curNode = headerList->mlh_Head; curNode->mln_Succ; curNode = curNode->mln_Succ)
            {
              struct HeaderNode *hdrNode = (struct HeaderNode *)curNode;
              char *field = hdrNode->name;
              char *value = hdrNode->content;

              if(!stricmp(field, "from"))
                msgdesc = StrBufCat(StrBufCat(msgdesc, GetStr(MSG_RE_MDNFrom)), value);
              else if(!stricmp(field, "to"))
                msgdesc = StrBufCat(StrBufCat(msgdesc, GetStr(MSG_RE_MDNTo)), value);
              else if(!stricmp(field, "subject"))
                msgdesc = StrBufCat(StrBufCat(msgdesc, GetStr(MSG_RE_MDNSubject)), value);
              else if(!stricmp(field, "original-message-id"))
                msgdesc = StrBufCat(StrBufCat(msgdesc, GetStr(MSG_RE_MDNMessageID)), value);
              else if(!stricmp(field, "date"))
                msgdesc = StrBufCat(StrBufCat(msgdesc, GetStr(MSG_RE_MDNDate)), value);
              else if(!stricmp(field, "original-recipient"))
                msgdesc = StrBufCat(StrBufCat(msgdesc, GetStr(MSG_RE_MDNOrigRecpt)), value);
              else if(!stricmp(field, "final-recipient"))
                msgdesc = StrBufCat(StrBufCat(msgdesc, GetStr(MSG_RE_MDNFinalRecpt)), value);
              else if(!stricmp(field, "disposition"))
                strlcpy(MDNtype, Trim(value), sizeof(MDNtype));
             }
          }

          FreeHeaderList(headerList);
          free(headerList);
        }
      }
    }

    msgdesc = StrBufCat(msgdesc, "\n");

    if(!strnicmp(MDNtype, "manual-action", 13))
      mode = GetStr(MSG_RE_MDNmanual);
    else if(!strnicmp(MDNtype, "automatic-action", 16))
      mode = GetStr(MSG_RE_MDNauto);

    if((type = strchr(MDNtype, ';')))
      type = Trim(++type);
    else
      type = MDNtype;

    snprintf(file, sizeof(file), "YAMm%08lx-p%d.txt", readMailDataID(rp[0]->rmData), rp[0]->Nr);
    strmfp(buf, C->TempDir, file);

    D(DBF_MAIL, "creating MDN report in '%s'", buf);

    if((out = fopen(buf, "w")))
    {
      if     (!stricmp(type, "displayed"))  fprintf(out, GetStr(MSG_RE_MDNdisplay), msgdesc);
      else if(!stricmp(type, "processed"))  fprintf(out, GetStr(MSG_RE_MDNprocessed), msgdesc, mode);
      else if(!stricmp(type, "dispatched")) fprintf(out, GetStr(MSG_RE_MDNdispatched), msgdesc, mode);
      else if(!stricmp(type, "deleted"))    fprintf(out, GetStr(MSG_RE_MDNdeleted), msgdesc, mode);
      else if(!stricmp(type, "denied"))     fprintf(out, GetStr(MSG_RE_MDNdenied), msgdesc);
      else fprintf(out, GetStr(MSG_RE_MDNunknown), msgdesc, type, mode);
      fclose(out);

      DeleteFile(rp[0]->Filename);
      strlcpy(rp[0]->Filename, buf, sizeof(rp[0]->Filename));
      rp[0]->Decoded = TRUE;
      RE_SetPartInfo(rp[0]);
      if(rp[2])
        RE_UndoPart(rp[2]);

      RE_UndoPart(rp[1]);
    }

    FreeStrBuf(msgdesc);
  }
}
///
/// RE_HandleSignedMessage
//  Handles a PGP signed message, checks validity of signature
static void RE_HandleSignedMessage(struct Part *frp)
{
   struct Part *rp[2];

   if ((rp[0] = frp->Next))
   {
      if (G->PGPVersion && (rp[1] = rp[0]->Next))
      {
         int error;
         struct TempFile *tf = OpenTempFile(NULL);
         char options[SIZE_LARGE];

         // flag the mail as having a PGP signature within the MIME encoding
         SET_FLAG(frp->rmData->signedFlags, PGPS_MIME);

         ConvertCRLF(rp[0]->Filename, tf->Filename, TRUE);
         snprintf(options, sizeof(options), (G->PGPVersion == 5) ? "%s -o %s +batchmode=1 +force +language=us" : "%s %s +bat +f +lang=en", rp[1]->Filename, tf->Filename);
         error = PGPCommand((G->PGPVersion == 5) ? "pgpv": "pgp", options, NOERRORS|KEEPLOG);
         if(error > 0)
           SET_FLAG(frp->rmData->signedFlags, PGPS_BADSIG);

         if(error >= 0)
           RE_GetSigFromLog(frp->rmData, NULL);

         tf->FP = NULL;
         CloseTempFile(tf);
      }
      RE_DecodePart(rp[0]);
   }
}
///
/// RE_DecryptPGP
//  Decrypts a PGP encrypted file
static int RE_DecryptPGP(struct ReadMailData *rmData, char *src)
{
  FILE *fh;
  int error;
  char options[SIZE_LARGE], orcpt[SIZE_ADDRESS];

  *orcpt = 0;
  PGPGetPassPhrase();

  if(G->PGPVersion == 5)
  {
    char fname[SIZE_PATHFILE];
    snprintf(fname, sizeof(fname), "%s.asc", src); Rename(src, fname);
    snprintf(options, sizeof(options), "%s +batchmode=1 +force +language=us", fname);
    error = PGPCommand("pgpv", options, KEEPLOG|NOERRORS);
    RE_GetSigFromLog(rmData, orcpt);
    if(*orcpt)
      error = 2;

    DeleteFile(fname);
  }
  else
  {
    snprintf(options, sizeof(options), "%s +bat +f +lang=en", src);
    error = PGPCommand("pgp", options, KEEPLOG|NOERRORS);
    RE_GetSigFromLog(rmData, NULL);
  }

  PGPClearPassPhrase(error < 0 || error > 1);
  if((error < 0 || error > 1) && (fh = fopen(src, "w")))
  {
    fputs(GetStr(MSG_RE_PGPNotAllowed), fh);

    if(G->PGPVersion == 5 && *orcpt)
      fprintf(fh, GetStr(MSG_RE_MsgReadOnly), orcpt);

    fclose(fh);
  }

  return error;
}
///
/// RE_HandleEncryptedMessage
//  Handles a PGP encryped message
static void RE_HandleEncryptedMessage(struct Part *frp)
{
   struct Part *warnPart;
   struct Part *encrPart;

   // if we find a warning and a encryption part we start decrypting
   if((warnPart = frp->Next) && (encrPart = warnPart->Next))
   {
      int decryptResult;
      struct TempFile *tf = OpenTempFile("w");

      // first we copy our encrypted part because the DecryptPGP()
      // function will overwrite it
      if(!tf || !CopyFile(NULL, tf->FP, encrPart->Filename, NULL)) return;
      fclose(tf->FP);
      tf->FP = NULL;

      decryptResult = RE_DecryptPGP(frp->rmData, tf->Filename);

      if(decryptResult == 1 || decryptResult == 0)
      {
         FILE *in;

         if(decryptResult == 0)
           SET_FLAG(frp->rmData->signedFlags, PGPS_MIME);

         SET_FLAG(frp->rmData->encryptionFlags, PGPE_MIME);

         // if DecryptPGP() returns with 0 everything worked perfectly and we can
         // convert & copy our decrypted file over the encrypted part
         if(ConvertCRLF(tf->Filename, warnPart->Filename, FALSE) && (in = fopen(warnPart->Filename, "r")))
         {
            if(warnPart->ContentType)
              free(warnPart->ContentType);

            warnPart->ContentType = strdup("text/plain");
            warnPart->Printable = TRUE;
            warnPart->EncodingCode = ENC_NONE;
            *warnPart->Description = '\0';
            RE_ScanHeader(warnPart, in, NULL, 2);
            fclose(in);
            warnPart->Decoded = FALSE;
            RE_DecodePart(warnPart);
            RE_UndoPart(encrPart); // undo the encrypted part because we have a decrypted now.
         }
      }
      else
      {
        // if we end up here the DecryptPGP returned an error an
        // we have to put this error in place were the nonlocalized version is right now.
        if(CopyFile(warnPart->Filename, NULL, tf->Filename, NULL))
        {
           if(warnPart->ContentType)
             free(warnPart->ContentType);

           warnPart->ContentType = strdup("text/plain");
           warnPart->Printable = TRUE;
           warnPart->EncodingCode = ENC_NONE;
           *warnPart->Description = '\0';
           warnPart->Decoded = TRUE;
           warnPart->HasHeaders = FALSE;
        }
      }

      CloseTempFile(tf);
   }
}
///
/// RE_LoadMessagePart
//  Decodes a single message part
static void RE_LoadMessagePart(struct ReadMailData *rmData, struct Part *part)
{
   int rsh = RE_RequiresSpecialHandling(part);

   switch (rsh)
   {
      case 1:
      {
        RE_HandleMDNReport(part);
      }
      break;

      case 2:
      {
        RE_HandleSignedMessage(part);
      }
      break;

      case 3:
      {
        RE_HandleEncryptedMessage(part);
      }
      break;

      default:
      {
        struct Part *rp;

        for(rp = part->Next; rp; rp = rp->Next)
        {
          if(stricmp(rp->ContentType, "application/pgp-keys") == 0)
          {
            rmData->hasPGPKey = TRUE;
          }
          else if(rp->Nr == PART_RAW || rp->Nr == rmData->letterPartNum ||
                  (rp->Printable && C->DisplayAllTexts)
                 )
          {
            RE_DecodePart(rp);
          }
        }
      }
   }
}
///
/// RE_LoadMessage
// Function that preparses a mail for either direct display in a read mail group
// or for background parsing (for Arexx and stuff)
BOOL RE_LoadMessage(struct ReadMailData *rmData)
{
  struct Mail *mail = rmData->mail;
  struct Folder *folder = mail->Folder;
  struct Part *part;
  BOOL result = FALSE;

  if(hasFlag(rmData->parseFlags, PM_QUIET) == FALSE)
    BusyText(GetStr(MSG_BusyReading), "");

  // here we read in the mail in our read mail group
  GetMailFile(rmData->readFile, folder, mail);

  // check whether the folder of the mail is using XPK and if so we
  // unpack it to a temporarly file
  if(isVirtualMail(mail) == FALSE &&
     isXPKFolder(folder))
  {
    char tmpFile[SIZE_PATHFILE];

    if(!StartUnpack(rmData->readFile, tmpFile, folder))
    {
      if(hasFlag(rmData->parseFlags, PM_QUIET) == FALSE)
        BusyEnd();

      return FALSE;
    }

    strlcpy(rmData->readFile, tmpFile, sizeof(rmData->readFile));
  }

  if((part = rmData->firstPart = RE_ParseMessage(rmData, NULL, rmData->readFile, NULL)))
  {
    int i;

    RE_LoadMessagePart(rmData, part);

    for(i = 0; part; i++, part = part->Next)
    {
      if(part->Nr != i)
      {
        char tmpFile[SIZE_PATHFILE];
        char file[SIZE_FILE];

        part->Nr = i;
        snprintf(file, sizeof(file), "YAMm%08lx-p%d%s", readMailDataID(rmData), i, strchr(part->Filename, '.'));
        strmfp(tmpFile, C->TempDir, file);

        D(DBF_MAIL, "renaming '%s' to '%s'", part->Filename, tmpFile);

        RenameFile(part->Filename, tmpFile);
        strlcpy(part->Filename, tmpFile, sizeof(part->Filename));
      }
    }

    if(i > 0)
      result = TRUE;

    // now we do a check because there might be mails with no actual
    // readable letterPart - however, they just have one part and it
    // is just a binary attachment (perhaps a jpg). For these mails we do
    // have to do things a bit different and still flag them as being a
    // multipart mail so that an attachment icon is shown.
    if(rmData->letterPartNum == 0 && i == 2)
    {
      // we copy some meta information from the firstPart to
      // the actual part containing the attachment.
      struct Part *firstPart = rmData->firstPart;
      struct Part *attachPart = firstPart->Next;

      strlcpy(attachPart->Name, firstPart->Name, sizeof(attachPart->Name));
      strlcpy(attachPart->Description, firstPart->Description, sizeof(attachPart->Description));

      if(attachPart->CParFileName)
        free(attachPart->CParFileName);

      attachPart->CParFileName = firstPart->CParFileName ? strdup(firstPart->CParFileName) : NULL;

      // in case the mail is already flagged as a
      // multipart mail we don't have to go on...
      if(isMultiPartMail(mail) == FALSE)
      {
        // set the MultiPart-Mixed flag
        SET_FLAG(mail->mflags, MFLAG_MP_MIXED);

        // if the mail is no virtual mail we can also
        // refresh the maillist depending information
        if(!isVirtualMail(mail))
        {
          SET_FLAG(mail->Folder->Flags, FOFL_MODIFY);  // flag folder as modified
          DoMethod(G->MA->GUI.PG_MAILLIST, MUIM_MainMailListGroup_RedrawMail, mail);
        }
      }
    }
  }

  if(hasFlag(rmData->parseFlags, PM_QUIET) == FALSE)
    BusyEnd();

  return result;
}

///
/// RE_ReadInMessage
//  Reads a message into a dynamic buffer and returns this buffer.
//  The text returned should *NOT* contain any MUI specific escape sequences, as
//  we will later parse the buffer again before we put it into the texteditor. So no deeep lexical analysis
//  are necessary here.
char *RE_ReadInMessage(struct ReadMailData *rmData, enum ReadInMode mode)
{
  struct Part *first;
  struct Part *last;
  struct Part *part;
  struct Part *uup = NULL;
  char *cmsg;
  int totsize, len;

  ENTER();

  D(DBF_MAIL, "rmData: 0x%08lx, mode: %d", rmData, mode);

  // save exit conditions
  if(!rmData || !(first = rmData->firstPart))
  {
    RETURN(NULL);
    return NULL;
  }

  SHOWVALUE(DBF_MAIL, rmData->letterPartNum);

  // first we precalculate the size of the final buffer where the message text will be put in
  for(totsize = 1000, part = first; part; part = part->Next)
  {
    // in non-READ mode (Reply, Forward etc) we do have to count only the sizes of
    // the RAW and LetterPart
    if(mode != RIM_READ && part->Nr != PART_RAW && part->Nr != rmData->letterPartNum)
      continue;

    if(part->Decoded || part->Nr == PART_RAW)
      totsize += part->Size;
    else
      totsize += 200;
  }

  // then we generate our final buffer for the message
  if((cmsg = calloc(len=(totsize*3)/2, sizeof(char))))
  {
    int wptr=0, prewptr;

    // if this function wasn`t called with QUIET we place a BusyText into the Main Window
    if(mode != RIM_QUIET)
      BusyText(GetStr(MSG_BusyDisplaying), "");

    // then we copy the first part (which is the header of the mail
    // into our final buffer because we don`t need to preparse it. However, we just
    // have to do it in RIM_PRINT mode because all other modes do take
    // respect of the headerList
    if(mode == RIM_PRINT)
    {
      FILE *fh;

      if((fh = fopen(first->Filename, "r")))
      {
        int buflen = first->MaxHeaderLen+4;
        char *linebuf = malloc(buflen);
        while(fgets(linebuf, buflen, fh))
        {
          cmsg = AppendToBuffer(cmsg, &wptr, &len, linebuf);
        }
        free(linebuf);
        fclose(fh);
        cmsg = AppendToBuffer(cmsg, &wptr, &len, "\n");
      }
    }

    // Now we check every part of the message if it will be displayed in the
    // texteditor or not and if so we run the part through the lexer
    for(part = first->Next; part; part = part->Next)
    {
      BOOL dodisp = (part->Nr == PART_RAW || part->Nr == rmData->letterPartNum) ||
                    (part->Printable && C->DisplayAllTexts && part->Decoded);

      // before we go on we check whether this is an alternative multipart
      // and if so we check that we only display the plain text one
      if(dodisp)
      {
        if(C->DisplayAllAltPart == FALSE &&
           (part->isAltPart == TRUE && part->Parent != NULL && part->Parent->MainAltPart != part))
        {
          D(DBF_MAIL, "flagging part #%d as hided.", part->Nr);

          dodisp = FALSE;
        }
      }

      prewptr = wptr;

      // if we are in READ mode and other parts than the LETTER part
      // should be displayed in the texteditor as well, we drop a simple separator bar with info.
      // This is used for attachments and here escape sequences are allowed as we don`t want them
      // to get stripped if the user selects "NoTextStyles"
      if(part->Nr != PART_RAW && part->Nr != rmData->letterPartNum)
      {
        if(mode != RIM_READ)
          continue;
        else if(dodisp)
        {
          char buffer[SIZE_LARGE];

          // lets generate the separator bar.
          snprintf(buffer, sizeof(buffer), "\n\033c\033[s:18]\033p[7]%s:%s%s\033p[0]\n"
                                           "\033l\033b%s:\033n %s <%s>\n", GetStr(MSG_MA_ATTACHMENT),
                                                                           *part->Name ? " " : "",
                                                                           part->Name,
                                                                           GetStr(MSG_RE_ContentType),
                                                                           DescribeCT(part->ContentType),
                                                                           part->ContentType);

          cmsg = AppendToBuffer(cmsg, &wptr, &len, buffer);

          *buffer = 0;
          if(*part->Description)
            snprintf(buffer, sizeof(buffer), "\033b%s:\033n %s\n", GetStr(MSG_RE_Description), part->Description);

          strlcat(buffer, "\033[s:2]\n", sizeof(buffer));
          cmsg = AppendToBuffer(cmsg, &wptr, &len, buffer);
        }
      }

      D(DBF_MAIL, "Checking if part #%d [%ld] (%d) should be displayed", part->Nr, part->Size, dodisp);

      // only continue of this part should be displayed
      // and is greater than zero, or else we don`t have
      // to parse anything at all.
      if(dodisp && part->Size > 0)
      {
        FILE *fh;

        D(DBF_MAIL, "  adding text of [%s] to display", part->Filename);

        if((fh = fopen(part->Filename, "r")))
        {
          char *msg;

          if((msg = calloc((size_t)(part->Size+3), sizeof(char))))
          {
            char *ptr, *rptr, *eolptr, *sigptr = 0;
            int nread;

            *msg = '\n';
            nread = fread(msg+1, 1, (size_t)(part->Size), fh);

            // lets check if an error or short item count occurred
            if(nread == 0 || nread != part->Size)
            {
              W(DBF_MAIL, "Warning: EOF or short item count detected: feof()=%ld ferror()=%ld", feof(fh), ferror(fh));

              // distinguish between EOF and error
              if(feof(fh) == 0 && ferror(fh) != 0)
              {
                // an error occurred, lets signal it by returning NULL
                E(DBF_MAIL, "ERROR occurred while reading at pos %ld of '%s'", ftell(fh), part->Filename);

                // cleanup and return NULL
                free(msg);
                fclose(fh);

                if(mode != RIM_QUIET)
                  BusyEnd();

                RETURN(NULL);
                return NULL;
              }

              // if we end up here it is "just" an EOF so lets put out
              // a warning and continue.
              W(DBF_MAIL, "Warning: EOF detected at pos %ld of '%s'", ftell(fh), part->Filename);
            }

            // now we analyze if that part of the mail text contains HTML
            // tags or not so that our HTML converter routines can be fired
            // up accordingly.
            if(C->ConvertHTML == TRUE && (mode == RIM_EDIT || mode == RIM_QUOTE || mode == RIM_READ) &&
               part->ContentType != NULL && stricmp(part->ContentType, "text/html") == 0)
            {
              char *converted;

              D(DBF_MAIL, "converting HTMLized part #%ld to plain-text", part->Nr);

              // convert all HTML stuff to plain text
              converted = html2mail(msg+1);

              // free the old HTMLized message
              free(msg);

              // overwrite the old values
              nread = strlen(converted);
              msg = converted;

              rptr = msg;
            }
            else
              rptr = msg+1; // nothing serious happened so lets continue...

            // find signature first if it should be stripped
            if(mode == RIM_QUOTE && C->StripSignature)
            {
              sigptr = msg + nread;
              while(sigptr > msg)
              {
                sigptr--;
                while((sigptr > msg) && (*sigptr != '\n')) sigptr--;  // step back to previous line

                if((sigptr <= msg+1))
                {
                  sigptr = NULL;
                  break;
                }

                if(strncmp(sigptr+1, "-- \n", 4) == 0)                // check for sig separator
                {                                                     // per definition it is a "-- " on a single line
                  sigptr++;
                  break;
                }
              }
            }

            // parse the message string
            while(*rptr)
            {
              BOOL newlineAtEnd;

              // lets get the first real line of the data and make sure to strip all
              // NUL bytes because otherwise we are not able to show the text.
              for(eolptr = rptr; *eolptr != '\n' && eolptr < msg+nread+1; eolptr++)
              {
                // strip null bytes that are in between the start and end of stream
                // here we simply exchange it by a space
                if(*eolptr == '\0')
                  *eolptr = ' ';
              }

              // check if we have a newline at the end or not
              newlineAtEnd = (*eolptr == '\n');

              // terminate the string right where we are (also eventually stripping
              // a newline
              *eolptr = '\0';

              // now that we have a full line we can check for inline stuff
              // like inline uuencode/pgp sections

/* UUenc */   if(mode != RIM_EDIT && // in Edit mode we don't handle inlined UUencoded attachments.
                 strncmp(rptr, "begin ", 6) == 0 &&
                 rptr[6] >= '0' && rptr[6] <= '7' &&
                 rptr[7] >= '0' && rptr[7] <= '7' &&
                 rptr[8] >= '0' && rptr[8] <= '7')
              {
                FILE *outfh;
                char *nameptr = NULL;

                D(DBF_MAIL, "inline UUencoded passage found!");

                // now we have to get the filename off the 'begin' line
                // so that we can put our new part together
                if(rptr[9] == ' ' && rptr[10] != '\0')
                  nameptr = &rptr[10];

                // find the currently last part of the message to where we attach
                // the new part now
                for(last = first; last->Next; last = last->Next);

                // then create the new part to which we will put our uudecoded
                // data
                if((outfh = RE_OpenNewPart(rmData, &uup, last, first)))
                {
                  char *endptr = rptr+strlen(rptr)+1;
                  long old_pos;

                  // prepare our part META data and fake the new part as being
                  // a application/octet-stream part as we don't know if it
                  // is some text or something else.
                  if(uup->ContentType)
                    free(uup->ContentType);

                  uup->ContentType = strdup("application/octet-stream");
                  strlcpy(uup->Description, GetStr(MSG_RE_UUencodedFile), sizeof(uup->Description));

                  if(nameptr)
                    strlcpy(uup->Name, nameptr, sizeof(uup->Name));

                  // save the old position of our input file position so that
                  // we can set it back later on
                  old_pos = ftell(fh);

                  // then let us seek to the position where we found the starting
                  // "begin" indicator
                  if(old_pos >= 0 &&
                     fseek(fh, rptr-msg-1, SEEK_SET) == 0)
                  {
                    // now that we are on the correct position, we
                    // call the uudecoding function accordingly.
                    long decoded = uudecode_file(fh, outfh, NULL); // no translation table
                    D(DBF_MAIL, "UU decoded %ld chars of part %ld.", decoded, uup->Nr);

                    if(decoded >= 0)
                      uup->Decoded = TRUE;
                    else
                    {
                      switch(decoded)
                      {
                        case -1:
                        {
                          if(hasFlag(rmData->parseFlags, PM_QUIET) == FALSE)
                            ER_NewError(GetStr(MSG_ER_UnexpEOFUU));
                        }
                        break;

                        case -2:
                        {
                          if(hasFlag(rmData->parseFlags, PM_QUIET) == FALSE)
                            ER_NewError(GetStr(MSG_ER_UUDEC_TAGMISS), uup->Filename, "begin");
                        }
                        break;

                        case -3:
                        {
                          if(hasFlag(rmData->parseFlags, PM_QUIET) == FALSE)
                            ER_NewError(GetStr(MSG_ER_InvalidLength), 0);
                        }
                        break;

                        case -4:
                        {
                          if(hasFlag(rmData->parseFlags, PM_QUIET) == FALSE)
                            ER_NewError(GetStr(MSG_ER_UUDEC_CHECKSUM), uup->Filename);

                          uup->Decoded = TRUE; // allow to save the resulting file
                        }
                        break;

                        case -5:
                        {
                          if(hasFlag(rmData->parseFlags, PM_QUIET) == FALSE)
                            ER_NewError(GetStr(MSG_ER_UUDEC_CORRUPT), uup->Filename);
                        }
                        break;

                        case -6:
                        {
                          if(hasFlag(rmData->parseFlags, PM_QUIET) == FALSE)
                            ER_NewError(GetStr(MSG_ER_UUDEC_TAGMISS), uup->Filename, "end");

                          uup->Decoded = TRUE; // allow to save the resulting file
                        }
                        break;

                        default:
                        {
                          if(hasFlag(rmData->parseFlags, PM_QUIET) == FALSE)
                            ER_NewError(GetStr(MSG_ER_UnexpEOFUU));
                        }
                        break;
                      }
                    }
                  }

                  // set back the old position to the filehandle
                  fseek(fh, old_pos, SEEK_SET);

                  // close our part filehandle
                  fclose(outfh);

                  // refresh the partinfo
                  RE_SetPartInfo(uup);

                  // if everything was fine we try to find the end marker
                  if(uup->Decoded)
                  {
                    // unfortunatly we have to find our ending "end" line now
                    // with an expensive string function. But this shouldn't be
                    // a problem as inline uuencoded parts are very rare today.
                    while((endptr = strstr(endptr, "\nend")))
                    {
                      endptr += 4; // point to the char after end

                      // skip eventually existing whitespaces
                      while(*endptr == ' ')
                        endptr++;

                      // now check if the terminating char is a newline or not
                      if(*endptr == '\n')
                        break;
                    }

                    // check if we found the terminating "end" line or not
                    if(endptr)
                    {
                      // then starting from the next line there should be the "size" line
                      if(*(++endptr) && strncmp(endptr, "size ", 5) == 0)
                      {
                        int expsize = atoi(&endptr[5]);

                        if(uup->Size != expsize)
                        {
                          if(hasFlag(rmData->parseFlags, PM_QUIET) == FALSE)
                            ER_NewError(GetStr(MSG_ER_UUSize), uup->Size, expsize);
                        }
                      }
                    }
                    else
                    {
                      if(hasFlag(rmData->parseFlags, PM_QUIET) == FALSE)
                        ER_NewError(GetStr(MSG_ER_UUDEC_TAGMISS), uup->Filename, "end");

                      endptr = rptr;
                    }
                  }
                  else endptr = rptr;

                  // find the end of the line
                  for(eolptr = endptr; *eolptr && *eolptr != '\n'; eolptr++);

                  // terminate the end
                  *eolptr = '\0';
                }
                else
                  ER_NewError(GetStr(MSG_ER_CantCreateTempfile));
              }
/* PGP msg */ else if(!strncmp(rptr, "-----BEGIN PGP MESSAGE", 21))
              {
                struct TempFile *tf;
                D(DBF_MAIL, "inline PGP encrypted message found");

                if((tf = OpenTempFile("w")))
                {
                  *eolptr = '\n';
                  for(ptr=eolptr+1; *ptr; ptr++)
                  {
                    if(!strncmp(ptr, "-----END PGP MESSAGE", 19)) break;

                    while (*ptr && *ptr != '\n') ptr++;
                  }

                  while (*ptr && *ptr != '\n') ptr++;

                  eolptr = ptr++;
                  fwrite(rptr, 1, (size_t)(ptr-rptr), tf->FP);
                  fclose(tf->FP);
                  tf->FP = NULL;

                  D(DBF_MAIL, "decrypting");

                  if(RE_DecryptPGP(rmData, tf->Filename) == 0)
                  {
                    // flag the mail as having a inline PGP signature
                    SET_FLAG(rmData->signedFlags, PGPS_OLD);

                    // make sure that the mail is flaged as signed
                    if(!isMP_SignedMail(rmData->mail))
                    {
                      SET_FLAG(rmData->mail->mflags, MFLAG_MP_SIGNED);
                      SET_FLAG(rmData->mail->Folder->Flags, FOFL_MODIFY);  // flag folder as modified
                    }
                  }

                  if ((tf->FP = fopen(tf->Filename, "r")))
                  {
                    char buf2[SIZE_LARGE];
                    D(DBF_MAIL, "decrypted message follows:");

                    while(fgets(buf2, SIZE_LARGE, tf->FP))
                    {
                      rptr = buf2;
                      D(DBF_MAIL, "%s", buf2);
                      cmsg = AppendToBuffer(cmsg, &wptr, &len, buf2);
                    }
                  }
                  CloseTempFile(tf);
                }

                // flag the mail as being inline PGP encrypted
                SET_FLAG(rmData->encryptionFlags, PGPE_OLD);

                // make sure that mail is flagged as crypted
                if(!isMP_CryptedMail(rmData->mail))
                {
                  SET_FLAG(rmData->mail->mflags, MFLAG_MP_CRYPT);
                  SET_FLAG(rmData->mail->Folder->Flags, FOFL_MODIFY);  // flag folder as modified
                }

                D(DBF_MAIL, "done with decryption");
              }
/* Signat. */ else if(!strcmp(rptr, "-- "))
              {
                if (mode == RIM_READ)
                {
                  if(C->SigSepLine == 1)
                    cmsg = AppendToBuffer(cmsg, &wptr, &len, rptr);
                  else if(C->SigSepLine == 2)
                    cmsg = AppendToBuffer(cmsg, &wptr, &len, "\033[s:2]");
                  else if(C->SigSepLine == 3)
                    break;

                  if(newlineAtEnd)
                    cmsg = AppendToBuffer(cmsg, &wptr, &len, "\n");
                }
                else if(mode == RIM_QUOTE)
                {
                  if(C->StripSignature && (rptr == sigptr))
                    break;
                }
                else
                {
                  if(newlineAtEnd)
                    cmsg = AppendToBuffer(cmsg, &wptr, &len, "-- \n");
                  else
                    cmsg = AppendToBuffer(cmsg, &wptr, &len, "-- ");
                }
              }
/* PGP sig */ else if (!strncmp(rptr, "-----BEGIN PGP PUBLIC KEY BLOCK", 31))
              {
                rmData->hasPGPKey = TRUE;

                cmsg = AppendToBuffer(cmsg, &wptr, &len, rptr);

                if(newlineAtEnd)
                  cmsg = AppendToBuffer(cmsg, &wptr, &len, "\n");
              }
              else if (!strncmp(rptr, "-----BEGIN PGP SIGNED MESSAGE", 29))
              {
                // flag the mail as having a inline PGP signature
                SET_FLAG(rmData->signedFlags, PGPS_OLD);

                if(!isMP_SignedMail(rmData->mail))
                {
                  SET_FLAG(rmData->mail->mflags, MFLAG_MP_SIGNED);
                  SET_FLAG(rmData->mail->Folder->Flags, FOFL_MODIFY);  // flag folder as modified
                }
              }
/* other */   else
              {
                cmsg = AppendToBuffer(cmsg, &wptr, &len, rptr);

                if(newlineAtEnd)
                  cmsg = AppendToBuffer(cmsg, &wptr, &len, "\n");
              }

              rptr = eolptr+1;
            }

            free(msg);
          }

          fclose(fh);
        }
      }
    }

    if(mode != RIM_QUIET)
      BusyEnd();
  }

  RETURN(cmsg);
  return cmsg;
}
///
/// RE_GetSenderInfo
//  Parses X-SenderInfo header field
void RE_GetSenderInfo(struct Mail *mail, struct ABEntry *ab)
{
   char *s, *t, *eq;
   struct ExtendedMail *email;

   memset(ab, 0, sizeof(struct ABEntry));
   strlcpy(ab->Address, mail->From.Address, sizeof(ab->Address));
   strlcpy(ab->RealName, mail->From.RealName, sizeof(ab->RealName));

   if(isSenderInfoMail(mail))
   {
      if((email = MA_ExamineMail(mail->Folder, mail->MailFile, TRUE)))
      {
        if ((s = strchr(email->SenderInfo, ';')))
        {
          *s++ = 0;
          do
          {
            if ((t = ParamEnd(s))) *t++ = 0;
            if (!(eq = strchr(s, '='))) Cleanse(s);
            else
            {
               *eq++ = 0;
               s = Cleanse(s); eq = TrimStart(eq);
               TrimEnd(eq);
               UnquoteString(eq, FALSE);
               if (!stricmp(s, "street"))   strlcpy(ab->Street, eq, sizeof(ab->Street));
               if (!stricmp(s, "city"))     strlcpy(ab->City, eq, sizeof(ab->City));
               if (!stricmp(s, "country"))  strlcpy(ab->Country, eq, sizeof(ab->Country));
               if (!stricmp(s, "phone"))    strlcpy(ab->Phone, eq, sizeof(ab->Phone));
               if (!stricmp(s, "homepage")) strlcpy(ab->Homepage, eq, sizeof(ab->Homepage));
               if (!stricmp(s, "dob"))      ab->BirthDay = atol(eq);
               if (!stricmp(s, "picture"))  strlcpy(ab->Photo, eq, sizeof(ab->Photo));
               ab->Type = 1;
            }
            s = t;
         }while (t);
       }
       MA_FreeEMailStruct(email);
     }
  }
}
///
/// RE_UpdateSenderInfo
//  Updates address book entry of sender
void RE_UpdateSenderInfo(struct ABEntry *old, struct ABEntry *new)
{
   BOOL changed = FALSE;

   if (!*old->RealName && *new->RealName) { strlcpy(old->RealName, new->RealName, sizeof(old->RealName)); changed = TRUE; }
   if (!*old->Address  && *new->Address ) { strlcpy(old->Address,  new->Address,  sizeof(old->Address));  changed = TRUE; }
   if (!*old->Street   && *new->Street  ) { strlcpy(old->Street,   new->Street,   sizeof(old->Street));   changed = TRUE; }
   if (!*old->Country  && *new->Country ) { strlcpy(old->Country,  new->Country,  sizeof(old->Country));  changed = TRUE; }
   if (!*old->City     && *new->City    ) { strlcpy(old->City,     new->City,     sizeof(old->City));     changed = TRUE; }
   if (!*old->Phone    && *new->Phone   ) { strlcpy(old->Phone,    new->Phone,    sizeof(old->Phone));    changed = TRUE; }
   if (!*old->Homepage && *new->Homepage) { strlcpy(old->Homepage, new->Homepage, sizeof(old->Homepage)); changed = TRUE; }
   if (!old->BirthDay  && new->BirthDay ) { old->BirthDay = new->BirthDay; changed = TRUE; }

   if (changed)
      CallHookPkt(&AB_SaveABookHook, 0, 0);
}
///
/// RE_AddToAddrbook
//  Adds sender to the address book
struct ABEntry *RE_AddToAddrbook(Object *win, struct ABEntry *templ)
{
   struct ABEntry ab_new;
   char buf[SIZE_LARGE];
   BOOL doit = FALSE;

   switch (C->AddToAddrbook)
   {
      case 1: if (!templ->Type) break;
      case 2: snprintf(buf, sizeof(buf), GetStr(MSG_RE_AddSender), BuildAddrName(templ->Address, templ->RealName));
              doit = MUI_Request(G->App, win, 0, NULL, GetStr(MSG_YesNoReq), buf);
              break;
      case 3: if (!templ->Type) break;
      case 4: doit = TRUE;
   }

   if (doit)
   {
      struct MUI_NListtree_TreeNode *tn = NULL;

      // first we check if the group for new entries already exists and if so
      // we add this address to this special group.
      if(C->NewAddrGroup[0])
      {
         tn = (struct MUI_NListtree_TreeNode *)DoMethod(G->AB->GUI.LV_ADDRESSES, MUIM_NListtree_FindName, MUIV_NListtree_FindName_ListNode_Root, C->NewAddrGroup, MUIF_NONE);

         // only if the group doesn`t exist yet
         if(!tn || ((struct ABEntry *)tn->tn_User)->Type != AET_GROUP)
         {
            memset(&ab_new, 0, sizeof(struct ABEntry));
            strlcpy(ab_new.Alias, C->NewAddrGroup, sizeof(ab_new.Alias));
            strlcpy(ab_new.Comment, GetStr(MSG_RE_NewGroupTitle), sizeof(ab_new.Comment));
            ab_new.Type = AET_GROUP;
            tn = (struct MUI_NListtree_TreeNode *)DoMethod(G->AB->GUI.LV_ADDRESSES, MUIM_NListtree_Insert, ab_new.Alias, &ab_new, MUIV_NListtree_Insert_ListNode_Root, MUIV_NListtree_Insert_PrevNode_Sorted, TNF_LIST);
         }
      }

      // then lets add the entry to the group that was perhaps
      // created previously.
      memset(&ab_new, 0, sizeof(struct ABEntry));
      ab_new.Type = AET_USER;
      RE_UpdateSenderInfo(&ab_new, templ);
      EA_SetDefaultAlias(&ab_new);
      tn = (struct MUI_NListtree_TreeNode *)DoMethod(G->AB->GUI.LV_ADDRESSES, MUIM_NListtree_Insert, ab_new.Alias, &ab_new, tn ? tn : MUIV_NListtree_Insert_ListNode_Root, MUIV_NListtree_Insert_PrevNode_Sorted, MUIF_NONE);
      if (tn)
      {
         CallHookPkt(&AB_SaveABookHook, 0, 0);
         return tn->tn_User;
      }
   }
   return NULL;
}
///
/// RE_FindPhotoOnDisk
//  Searches portrait of sender in the gallery directory
BOOL RE_FindPhotoOnDisk(struct ABEntry *ab, char *photo)
{
   *photo = '\0';
   if(*ab->Photo)
     strcpy(photo, ab->Photo);
   else if (*C->GalleryDir)
   {
      char fname[SIZE_FILE];
      strlcpy(fname, ab->RealName, sizeof(fname));
      if(PFExists(C->GalleryDir, fname))
        strmfp(photo, C->GalleryDir, fname);
      else
      {
         strlcpy(fname, ab->Address, sizeof(fname));
         if(PFExists(C->GalleryDir, fname))
           strmfp(photo, C->GalleryDir, fname);
      }
   }
   if (!*photo) return FALSE;
   return (BOOL)(FileSize(photo) > 0);
}
///
/// RE_ClickedOnMessage
//  User clicked on a e-mail address
void RE_ClickedOnMessage(char *address)
{
   struct ABEntry *ab = NULL;
   int l, win, hits;
   char *p, buf[SIZE_LARGE];
   char *body = NULL, *subject = NULL, *cc = NULL, *bcc = NULL;

   ENTER();
   SHOWSTRING(DBF_MAIL, address);

   // just prevent something bad from happening.
   if(!address || !(l = strlen(address)))
   {
     LEAVE();
     return;
   }

   // now we check for additional options to the mailto: string (if it is one)
   if((p = strchr(address, '?'))) *p++ = '\0';

   while(p)
   {
      if(!strnicmp(p, "subject=", 8))    subject = &p[8];
      else if(!strnicmp(p, "body=", 5))  body = &p[5];
      else if(!strnicmp(p, "cc=", 3))    cc = &p[3];
      else if(!strnicmp(p, "bcc=", 4))   bcc = &p[4];

      if((p = strchr(p, '&'))) *p++ = '\0';

      // now we check if this "&" is because of a "&amp;" which is the HTML code
      // for a "&" - we only handle this code because handling ALL HTML code
      // would be too complicated right now. we will support that later anyway.
      if(p && !strnicmp(p, "amp;", 4)) p+=4;
   }

   // please note that afterwards we should normally transfer HTML specific codes
   // like &amp; %20 aso. because otherwise links like mailto:Bilbo%20Baggins%20&lt;bilbo@baggins.de&gt;
   // will not work... but this is stuff we can do in one of the next versions.

   // lets see if we have an entry for that in the Addressbook
   // and if so, we reuse it
   hits = AB_SearchEntry(address, ASM_ADDRESS|ASM_USER|ASM_LIST, &ab);

   snprintf(buf, sizeof(buf), GetStr(MSG_RE_SelectAddressReq), address);

   switch (MUI_Request(G->App, G->MA->GUI.WI, 0, NULL, GetStr(hits ? MSG_RE_SelectAddressEdit : MSG_RE_SelectAddressAdd), buf))
   {
      case 1:
      {
        if ((win = MA_NewNew(NULL, 0)) >= 0)
        {
          struct WR_GUIData *gui = &G->WR[win]->GUI;

          setstring(gui->ST_TO, hits ? BuildAddrName(address, ab->RealName) : address);
          if (subject) setstring(gui->ST_SUBJECT, subject);
          if (body) set(gui->TE_EDIT, MUIA_TextEditor_Contents, body);
          if (cc) setstring(gui->ST_CC, cc);
          if (bcc) setstring(gui->ST_BCC, bcc);
          set(gui->WI, MUIA_Window_ActiveObject, gui->ST_SUBJECT);
        }
      }
      break;

      case 2:
      {
        DoMethod(G->App, MUIM_CallHook, &AB_OpenHook, ABM_EDIT, TAG_DONE);
        if (hits)
        {
          if ((win = EA_Init(ab->Type, ab)) >= 0) EA_Setup(win, ab);
        }
        else
        {
          if ((win = EA_Init(AET_USER, NULL)) >= 0) setstring(G->EA[win]->GUI.ST_ADDRESS, address);
        }
      }
      break;
   }

   LEAVE();
}
///

/*** GUI ***/
/// CreateReadWindow()
// Function that creates a new ReadWindow object and returns
// the referencing ReadMailData structure which was created
// during that process - or NULL if an error occurred.
struct ReadMailData *CreateReadWindow(BOOL forceNewWindow)
{
  Object *newReadWindow;

  ENTER();

  // if MultipleWindows support if off we try to reuse an already existing
  // readWindow
  if(forceNewWindow == FALSE &&
     C->MultipleWindows == FALSE &&
     IsMinListEmpty(&G->readMailDataList) == FALSE)
  {
    struct MinNode *curNode = G->readMailDataList.mlh_Head;

    D(DBF_GUI, "No MultipleWindows support, trying to reuse a window.");

    // search through our ReadDataList
    for(; curNode->mln_Succ; curNode = curNode->mln_Succ)
    {
      struct ReadMailData *rmData = (struct ReadMailData *)curNode;

      if(rmData->readWindow)
      {
        RETURN(rmData);
        return rmData;
      }
    }
  }

  D(DBF_GUI, "Creating new Read Window.");

  // if we end up here we create a new ReadWindowObject
  newReadWindow = ReadWindowObject, End;
  if(newReadWindow)
  {
    // get the ReadMailData and check that it is the same like created
    struct ReadMailData *rmData = (struct ReadMailData *)xget(newReadWindow, MUIA_ReadWindow_ReadMailData);

    if(rmData && rmData->readWindow == newReadWindow)
    {
      D(DBF_GUI, "Read window created: 0x%08lx", rmData);

      RETURN(rmData);
      return rmData;
    }

    DoMethod(G->App, OM_REMMEMBER, newReadWindow);
    MUI_DisposeObject(newReadWindow);
  }

  E(DBF_GUI, "ERROR occurred during read Window creation!");

  RETURN(NULL);
  return NULL;
}

///

/*** ReadMailData ***/
/// AllocPrivateRMData()
//  Allocates resources for background message parsing
struct ReadMailData *AllocPrivateRMData(struct Mail *mail, short parseFlags)
{
  struct ReadMailData *rmData = calloc(1, sizeof(struct ReadMailData));

  ENTER();

  if(rmData)
  {
    rmData->mail = mail;
    rmData->parseFlags = parseFlags;

    if(RE_LoadMessage(rmData) == FALSE)
    {
      free(rmData);
      rmData = NULL;
    }
  }

  RETURN(rmData);
  return rmData;
}
///
/// FreePrivateRMData()
//  Frees resources used by background message parsing
void FreePrivateRMData(struct ReadMailData *rmData)
{
  ENTER();

  if(CleanupReadMailData(rmData, FALSE))
    free(rmData);

  LEAVE();
}
///
/// CleanupReadMailData()
// cleans/deletes all data of a readmaildata structure
BOOL CleanupReadMailData(struct ReadMailData *rmData, BOOL fullCleanup)
{
  struct Part *part;
  struct Part *next;
  struct Mail *mail;

  ENTER();

  SHOWVALUE(DBF_MAIL, rmData);
  SHOWVALUE(DBF_MAIL, fullCleanup);
  ASSERT(rmData != NULL);

  // safe some pointer in advance
  mail = rmData->mail;

  // check if we also have to close an existing read window
  // or not.
  if(fullCleanup && rmData->readWindow)
  {
    D(DBF_MAIL, "make sure the window is closed");

    // make sure the window is really closed
    nnset(rmData->readWindow, MUIA_Window_Open, FALSE);

    // for other windows we have to save the GUI object weights
    // aswell
    if(rmData->readMailGroup)
    {
      G->Weights[2] = xget(rmData->readMailGroup, MUIA_ReadMailGroup_HGVertWeight);
      G->Weights[3] = xget(rmData->readMailGroup, MUIA_ReadMailGroup_TGVertWeight);
    }
  }

  // cleanup the parts and their temporarly files/memory areas
  for(part = rmData->firstPart; part; part = next)
  {
    next = part->Next;

    if(*part->Filename)
      DeleteFile(part->Filename);

    if(part->headerList)
    {
      FreeHeaderList(part->headerList);
      free(part->headerList);
      part->headerList = NULL;
    }

    free(part->ContentType);
    part->ContentType = NULL;

    free(part->ContentDisposition);
    part->ContentDisposition = NULL;

    // free all the CPar structue members
    if(part->CParName)      free(part->CParName);
    if(part->CParFileName)  free(part->CParFileName);
    if(part->CParBndr)      free(part->CParBndr);
    if(part->CParProt)      free(part->CParProt);
    if(part->CParDesc)      free(part->CParDesc);
    if(part->CParRType)     free(part->CParRType);
    if(part->CParCSet)      free(part->CParCSet);

    D(DBF_MAIL, "freeing mailpart: %08lx", part);
    free(part);
  }
  rmData->firstPart = NULL;

  SHOWVALUE(DBF_MAIL, rmData);

  // now clear some flags and stuff so that others may have a clean readmaildata
  // structure
  rmData->signedFlags = 0;
  rmData->encryptionFlags = 0;
  rmData->hasPGPKey = 0;
  rmData->letterPartNum = 0;

  SHOWVALUE(DBF_MAIL, rmData);
  SHOWPOINTER(DBF_MAIL, mail);
  D(DBF_MAIL, "isVirtual: %ld", mail != NULL ? isVirtualMail(mail) : FALSE);
  D(DBF_MAIL, "isXPK: %ld", mail != NULL && mail->Folder != NULL ? isXPKFolder(mail->Folder) : FALSE);

  // now we have to check whether there is a .unp (unpack) file and delete
  // it acoordingly (we can`t use the FinishUnpack() function because the
  // window still refers to the file which will prevent the deletion.
  if(mail != NULL && isVirtualMail(mail) == FALSE &&
     mail->Folder != NULL && isXPKFolder(mail->Folder))
  {
    char ext[SIZE_FILE];

    D(DBF_MAIL, "cleaning up unpacked mail file");

    stcgfe(ext, rmData->readFile);
    if(strcmp(ext, "unp") == 0)
      DeleteFile(rmData->readFile);
  }

  // if the caller wants to cleanup everything tidy we do it here or exit immediatly
  if(fullCleanup)
  {
    D(DBF_MAIL, "doing a full cleanup");

    // close any opened temporary file
    if(rmData->tempFile)
    {
      D(DBF_MAIL, "closing tempfile");
      CloseTempFile(rmData->tempFile);
      rmData->tempFile = NULL;
    }

    // if the rmData carries a virtual mail we have to clear it
    // aswell
    if(mail != NULL && isVirtualMail(mail))
    {
      D(DBF_MAIL, "freeing virtual mail pointer");
      free(mail);
    }

    // set the mail pointer to NULL
    rmData->mail = NULL;

    // clean up the read window now
    if(rmData->readWindow != NULL)
    {
      D(DBF_GUI, "cleaning up readwindow");
      DoMethod(G->App, OM_REMMEMBER, rmData->readWindow);
      MUI_DisposeObject(rmData->readWindow);
      rmData->readWindow = NULL;
    }
  }
  else
    rmData->mail = NULL;

  RETURN(TRUE);
  return TRUE;
}

///
/// FreeHeaderList()
// Free all items of an existing header list
void FreeHeaderList(struct MinList *headerList)
{
  struct MinNode *curNode;

  if(headerList == NULL || IsMinListEmpty(headerList) == TRUE)
    return;

  // Now we process the read header to set all flags accordingly
  for(curNode = headerList->mlh_Head; curNode->mln_Succ;)
  {
    struct HeaderNode *hdrNode = (struct HeaderNode *)curNode;

    // before we remove the node we have to save the pointer to the next one
    curNode = curNode->mln_Succ;

    // Remove node from list
    Remove((struct Node *)hdrNode);

    // Free everything of the node
    FreeStrBuf(hdrNode->name);
    FreeStrBuf(hdrNode->content);

    free(hdrNode);
  }
}

///
