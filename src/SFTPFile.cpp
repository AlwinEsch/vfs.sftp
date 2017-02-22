/*
 *      Copyright (C) 2005-2013 Team XBMC
 *      http://xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include <kodi/addon-instance/ExternVFS.h>
#include "threads/mutex.h"
#include "SFTPSession.h"

#include <map>
#include <sstream>

struct SFTPContext
{
  CSFTPSessionPtr session;
  sftp_file sftp_handle;
  std::string file;
};

class CSFTPFile
  : public kodi::addon::CInstanceExternVFS
{
public:
  CSFTPFile(KODI_HANDLE instance);

  virtual void* Open(VFSURL* url) override;
  virtual ssize_t Read(void* context, void* buffer, size_t uiBufSize) override;
  virtual int64_t Seek(void* context, int64_t position, int whence) override;
  virtual int64_t GetLength(void* context) override;
  virtual int64_t GetPosition(void* context) override;
  virtual int IoControl(void* context, XFILE::EIoControl request, void* param) override;
  virtual int Stat(VFSURL* url, struct __stat64* buffer) override;
  virtual bool Close(void* context) override;
  virtual bool Exists(VFSURL* url) override;
  virtual void ClearOutIdle() override;
  virtual void DisconnectAll() override;
  virtual bool DirectoryExists(VFSURL* url) override;
  virtual void* GetDirectory(VFSURL* url,
                              VFSDirEntry** entries,
                              int* num_entries,
                              VFSCallbacks* callbacks) override;
  virtual void FreeDirectory(void* ctx) override;
};

CSFTPFile::CSFTPFile(KODI_HANDLE instance) : kodi::addon::CInstanceExternVFS(instance)
{
}

void* CSFTPFile::Open(VFSURL* url)
{
  SFTPContext* result = new SFTPContext;

  result->session = CSFTPSessionManager::Get().CreateSession(url);

  if (result->session)
  {
    result->file = url->filename;
    result->sftp_handle = result->session->CreateFileHande(result->file);
    if (result->sftp_handle)
      return result;
  }

  kodi::Log(ADDON_LOG_ERROR, "SFTPFile: Failed to allocate session");
  delete result;
  return nullptr;
}

ssize_t CSFTPFile::Read(void* context, void* buffer, size_t uiBufSize)
{
  SFTPContext* ctx = (SFTPContext*)context;
  if (ctx && ctx->session && ctx->sftp_handle)
  {
    int rc = ctx->session->Read(ctx->sftp_handle, buffer, (size_t)uiBufSize);

    if (rc >= 0)
      return rc;
    else
      kodi::Log(ADDON_LOG_ERROR, "SFTPFile: Failed to read %i", rc);
  }
  else
    kodi::Log(ADDON_LOG_ERROR, "SFTPFile: Can't read without a filehandle");

  return -1;
}

int64_t CSFTPFile::Seek(void* context, int64_t iFilePosition, int whence)
{
  SFTPContext* ctx = (SFTPContext*)context;
  if (ctx && ctx->session && ctx->sftp_handle)
  {
    uint64_t position = 0;
    if (whence == SEEK_SET)
      position = iFilePosition;
    else if (whence == SEEK_CUR)
      position = GetPosition(context) + iFilePosition;
    else if (whence == SEEK_END)
      position = GetLength(context) + iFilePosition;

    if (ctx->session->Seek(ctx->sftp_handle, position) == 0)
      return GetPosition(context);
    else
      return -1;
  }

  kodi::Log(ADDON_LOG_ERROR, "SFTPFile: Can't seek without a filehandle");
  return -1;
}

int64_t CSFTPFile::GetLength(void* context)
{
  SFTPContext* ctx = (SFTPContext*)context;
  struct __stat64 buffer;
  if (ctx->session->Stat(ctx->file.c_str(), &buffer) != 0)
    return 0;

  return buffer.st_size;
}

int64_t CSFTPFile::GetPosition(void* context)
{
  SFTPContext* ctx = (SFTPContext*)context;
  if (ctx->session && ctx->sftp_handle)
    return ctx->session->GetPosition(ctx->sftp_handle);

  kodi::Log(ADDON_LOG_ERROR, "SFTPFile: Can't get position without a filehandle for '%s'", ctx->file.c_str());
  return 0;
}

int CSFTPFile::IoControl(void* context, XFILE::EIoControl request, void* param)
{
  if(request == XFILE::IOCTRL_SEEK_POSSIBLE)
    return 1;

  return -1;
}

int CSFTPFile::Stat(VFSURL* url, struct __stat64* buffer)
{
  CSFTPSessionPtr session = CSFTPSessionManager::Get().CreateSession(url);
  if (session)
    return session->Stat(url->filename, buffer);

  kodi::Log(ADDON_LOG_ERROR, "SFTPFile: Failed to create session to stat for '%s'", url->filename);
  return -1;
}

bool CSFTPFile::Close(void* context)
{
  SFTPContext* ctx = (SFTPContext*)context;
  if (ctx->session && ctx->sftp_handle)
    ctx->session->CloseFileHandle(ctx->sftp_handle);
  delete ctx;

  return true;
}

bool CSFTPFile::Exists(VFSURL* url)
{
  CSFTPSessionPtr session = CSFTPSessionManager::Get().CreateSession(url);
  if (session)
    return session->FileExists(url->filename);

  kodi::Log(ADDON_LOG_ERROR, "SFTPFile: Failed to create session to check exists for '%s'", url->filename);
  return false;
}

void CSFTPFile::ClearOutIdle()
{
  CSFTPSessionManager::Get().ClearOutIdleSessions();
}

void CSFTPFile::DisconnectAll()
{
  CSFTPSessionManager::Get().DisconnectAllSessions();
}

bool CSFTPFile::DirectoryExists(VFSURL* url)
{
  CSFTPSessionPtr session = CSFTPSessionManager::Get().CreateSession(url);
  if (session)
    return session->DirectoryExists(url->filename);

  kodi::Log(ADDON_LOG_ERROR, "SFTPFile: Failed to create session to check exists");
  return false;
}

void* CSFTPFile::GetDirectory(VFSURL* url,
                              VFSDirEntry** entries,
                              int* num_entries,
                              VFSCallbacks* callbacks)
{
  std::vector<VFSDirEntry>* result = new std::vector<VFSDirEntry>;
  CSFTPSessionPtr session = CSFTPSessionManager::Get().CreateSession(url);
  std::stringstream str;
  str << "sftp://" << url->username << ":" << url->password
      << "@" << url->hostname << ":" << url->port << "/";
  if (!session->GetDirectory(str.str(), url->filename, *result))
  {
    delete result;
    return NULL;
  }

  if (result->size())
    *entries = &(*result)[0];
  *num_entries = result->size();

  return result;
}

void CSFTPFile::FreeDirectory(void* items)
{
  std::vector<VFSDirEntry>& ctx = *(std::vector<VFSDirEntry>*)items;
  for (size_t i=0;i<ctx.size();++i)
  {
    free(ctx[i].label);
    for (size_t j=0;j<ctx[i].num_props;++j)
    {
      free(ctx[i].properties[j].name);
      free(ctx[i].properties[j].val);
    }
    delete ctx[i].properties;
    free(ctx[i].path);
  }
  delete &ctx;
}


class CMyAddon : public kodi::addon::CAddonBase
{
public:
  CMyAddon() { }
  virtual ADDON_STATUS CreateInstance(int instanceType, std::string instanceID, KODI_HANDLE instance, KODI_HANDLE& addonInstance) override
  {
    addonInstance = new CSFTPFile(instance);
    return ADDON_STATUS_OK;
  }
};

ADDONCREATOR(CMyAddon);
