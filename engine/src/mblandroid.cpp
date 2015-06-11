/* Copyright (C) 2003-2013 Runtime Revolution Ltd.

This file is part of LiveCode.

LiveCode is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License v3 as published by the Free
Software Foundation.

LiveCode is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with LiveCode.  If not see <http://www.gnu.org/licenses/>.  */

#include <android/log.h>
#include <dlfcn.h>

#include "prefix.h"

#include "system.h"
#include "globdefs.h"
#include "filedefs.h"
#include "objdefs.h"
#include "parsedef.h"
#include "globals.h"

#include "mblandroid.h"
#include "variable.h"

#include "socket.h"

////////////////////////////////////////////////////////////////////////////////

extern bool MCSystemLaunchUrl(MCStringRef p_url);

////////////////////////////////////////////////////////////////////////////////

uint1 *MClowercasingtable;
uint1 *MCuppercasingtable;

////////////////////////////////////////////////////////////////////////////////

bool MCAndroidSystem::Initialize(void)
{
    IO_stdin = MCsystem -> OpenFd(0, kMCOpenFileModeRead);
    IO_stdout = MCsystem -> OpenFd(1, kMCOpenFileModeWrite);
    IO_stderr = MCsystem -> OpenFd(2, kMCOpenFileModeWrite);
    
    // Initialize our case mapping tables
    
    MCuppercasingtable = new uint1[256];
    for(uint4 i = 0; i < 256; ++i)
        MCuppercasingtable[i] = (uint1)toupper((uint1)i);
    
    MClowercasingtable = new uint1[256];
    for(uint4 i = 0; i < 256; ++i)
        MClowercasingtable[i] = (uint1)tolower((uint1)i);
    
	return true;
}

void MCAndroidSystem::Finalize(void)
{
}

////////////////////////////////////////////////////////////////////////////////

MCServiceInterface *MCAndroidSystem::QueryService(MCServiceType type)
{
    return nil;
}

////////////////////////////////////////////////////////////////////////////////

void MCAndroidSystem::Debug(MCStringRef p_string)
{
    MCAutoStringRefAsUTF8String t_utf8_string;
    /* UNCHECKED */ t_utf8_string . Lock(p_string);
	__android_log_print(ANDROID_LOG_INFO, "LiveCode", "%s", *t_utf8_string);
}

////////////////////////////////////////////////////////////////////////////////

MCSysModuleHandle MCAndroidSystem::LoadModule(MCStringRef p_path)
{
	void *t_result;
    MCAutoStringRefAsUTF8String t_utf8_path;
    /* UNCHECKED */ t_utf8_path . Lock(p_path);
	t_result = dlopen(*t_utf8_path, RTLD_LAZY);
	MCLog("LoadModule(%s) - %p\n", *t_utf8_path, t_result);
	return (MCSysModuleHandle)t_result;
}

MCSysModuleHandle MCAndroidSystem::ResolveModuleSymbol(MCSysModuleHandle p_module, MCStringRef p_symbol)
{
    MCAutoStringRefAsUTF8String t_utf8_symbol;
    /* UNCHECKED */ t_utf8_symbol . Lock(p_symbol);
	return (MCSysModuleHandle)dlsym((void*)p_module, *t_utf8_symbol);
}

void MCAndroidSystem::UnloadModule(MCSysModuleHandle p_module)
{
	dlclose((void*)p_module);
}

////////////////////////////////////////////////////////////////////////////////

MCSystemInterface *MCMobileCreateAndroidSystem(void)
{
	return new MCAndroidSystem;
}

////////////////////////////////////////////////////////////////////////////////

Boolean MCAndroidSystem::GetDevices(MCStringRef& r_devices)
{
    return False;
}

Boolean MCAndroidSystem::GetDrives(MCStringRef& r_drives)
{
    return False;
}

void MCAndroidSystem::CheckProcesses(void)
{
    return;
}

bool MCAndroidSystem::StartProcess(MCNameRef p_name, MCStringRef p_doc, intenum_t p_mode, Boolean p_elevated)
{
    return false;
}

void MCAndroidSystem::CloseProcess(uint2 p_index)
{
    return;
}

void MCAndroidSystem::Kill(int4 p_pid, int4 p_sig)
{
    return;
}

void MCAndroidSystem::KillAll(void)
{
    return;
}

// MM-2015-06-08: [[ MobileSockets ]] Ported over poll code from OS X implemetation.
Boolean MCAndroidSystem::Poll(real8 p_delay, int p_fd)
{
    Boolean handled = False;
    fd_set rmaskfd, wmaskfd, emaskfd;
    FD_ZERO(&rmaskfd);
    FD_ZERO(&wmaskfd);
    FD_ZERO(&emaskfd);
    int4 maxfd = 0;
    if (!MCnoui)
    {
        if (p_fd != 0)
            FD_SET(p_fd, &rmaskfd);
        maxfd = p_fd;
    }
    if (MCshellfd != -1)
    {
        FD_SET(MCshellfd, &rmaskfd);
        if (MCshellfd > maxfd)
            maxfd = MCshellfd;
    }
    
    uint2 i;
    for (i = 0 ; i < MCnsockets ; i++)
    {
        int fd = MCsockets[i]->fd;
        if (!fd || MCsockets[i]->resolve_state == kMCSocketStateResolving ||
            MCsockets[i]->resolve_state == kMCSocketStateError)
            continue;
        if (MCsockets[i]->connected && !MCsockets[i]->closing
            && !MCsockets[i]->shared || MCsockets[i]->accepting)
            FD_SET(fd, &rmaskfd);
        if (!MCsockets[i]->connected || MCsockets[i]->wevents != NULL)
            FD_SET(fd, &wmaskfd);
        FD_SET(fd, &emaskfd);
        if (fd > maxfd)
            maxfd = fd;
        if (MCsockets[i]->added)
        {
            p_delay = 0.0;
            MCsockets[i]->added = False;
            handled = True;
        }
    }
    
    struct timeval timeoutval;
    timeoutval.tv_sec = (long)p_delay;
    timeoutval.tv_usec = (long)((p_delay - floor(p_delay)) * 1000000.0);
    int n = 0;
    
    n = select(maxfd + 1, &rmaskfd, &wmaskfd, &emaskfd, &timeoutval);
    
    if (n <= 0)
        return handled;
    
    if (MCshellfd != -1 && FD_ISSET(MCshellfd, &rmaskfd))
        return True;
    
    for (i = 0 ; i < MCnsockets ; i++)
    {
        int fd = MCsockets[i]->fd;
        if (FD_ISSET(fd, &emaskfd) && fd != 0)
        {
            
            if (!MCsockets[i]->waiting)
            {
                MCsockets[i]->error = strclone("select error");
                MCsockets[i]->doclose();
            }
            
        }
        else
        {
            if (FD_ISSET(fd, &rmaskfd) && !MCsockets[i]->shared)
            {
                MCsockets[i]->readsome();
            }
            if (FD_ISSET(fd, &wmaskfd))
            {
                MCsockets[i]->writesome();
            }
        }
        MCsockets[i]->setselect();
    }
    
    return True;
}

Boolean MCAndroidSystem::IsInteractiveConsole(int p_fd)
{
    return False;
}

void MCAndroidSystem::LaunchDocument(MCStringRef p_document)
{
    return;
}

void MCAndroidSystem::LaunchUrl(MCStringRef p_url)
{
    // AL-2014-06-26: [[ Bug 12700 ]] Implement launch url
	if (!MCSystemLaunchUrl(p_url))
        MCresult -> sets("no association");
}

void MCAndroidSystem::DoAlternateLanguage(MCStringRef p_script, MCStringRef p_language)
{
    return;
}

bool MCAndroidSystem::AlternateLanguages(MCListRef& r_list)
{
    return false;
}

bool MCAndroidSystem::GetDNSservers(MCListRef& r_list)
{
    return false;
}

////////////////////////////////////////////////////////////////////////////////
