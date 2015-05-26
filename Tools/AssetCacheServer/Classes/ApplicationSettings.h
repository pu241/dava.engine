/*==================================================================================
    Copyright (c) 2008, binaryzebra
    All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.
    * Neither the name of the binaryzebra nor the
    names of its contributors may be used to endorse or promote products
    derived from this software without specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE binaryzebra AND CONTRIBUTORS "AS IS" AND
    ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
    WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
    DISCLAIMED. IN NO EVENT SHALL binaryzebra BE LIABLE FOR ANY
    DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
    (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
    LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
    ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
    SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
=====================================================================================*/


#ifndef __APPLICATION_SETTINGS_H__
#define __APPLICATION_SETTINGS_H__

#include "AssetCache/AssetCacheConstants.h"
#include "FileSystem/FilePath.h"

#include <QObject>

namespace DAVA
{
    class KeyedArchive;
};


struct ServerData
{
    ServerData() = default;
    ServerData(DAVA::String _ip, DAVA::uint16 _port) : ip(_ip), port(_port) {};
    
    DAVA::String ip = "127.0.0.1";
    DAVA::uint16 port = DAVA::AssetCache::ASSET_SERVER_PORT;
};

class ApplicationSettings: public QObject
{
    Q_OBJECT
    
public:
    
    void Save() const;
    void Load();

    const DAVA::FilePath & GetFolder() const;
    void SetFolder(const DAVA::FilePath & folder);

    const DAVA::float64 GetCacheSize() const;
    void SetCacheSize(const DAVA::float64 size);

    const DAVA::uint32 GetFilesCount() const;
    void SetFilesCount(const DAVA::uint32 count);

    const DAVA::List<ServerData> & GetServers() const;
    void AddServer(const ServerData & server);
    void RemoveServer(const ServerData & server);
    
signals:
    
    void FolderChanged(const DAVA::FilePath & folder);
    void CacheSizeChanged(const DAVA::float64 cacheSize);
    void FilesCountChanged(const DAVA::uint32 filesCount);
    
    void ServersListChanged(const DAVA::List<ServerData> &);
    
private:

    void Serialize(DAVA::KeyedArchive * archieve) const;
    void Deserialize(DAVA::KeyedArchive * archieve);
    
    
public:

    DAVA::FilePath folder;
    DAVA::float64 cacheSize;
    DAVA::uint32 filesCount;

    DAVA::List<ServerData> servers;
};

#endif // __APPLICATION_SETTINGS_H__
