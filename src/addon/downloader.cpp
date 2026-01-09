//  SuperTux
//  Copyright (C) 2007 Christoph Sommer <christoph.sommer@2007.expires.deltadevelopment.de>
//                2014 Ingo Ruhnke <grumbel@gmail.com>
//                2023 Vankata453
//
//  This program is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include "addon/downloader.hpp"

#include <algorithm>
#include <array>
#include <assert.h>
#include <memory>
#include <physfs.h>
#include <sstream>
#include <stdexcept>
#include <version.h>

#include "physfs/util.hpp"
#include "supertux/gameconfig.hpp"
#include "supertux/globals.hpp"
#include "util/file_system.hpp"
#include "util/log.hpp"
#include "util/string_util.hpp"

namespace {

// This one is necessary for a download function.
size_t my_curl_string_append(void* ptr, size_t size, size_t nmemb, void* userdata)
{
  std::string& s = *static_cast<std::string*>(userdata);
  std::string buf(static_cast<char*>(ptr), size * nmemb);
  s += buf;
  log_debug << "read " << size * nmemb << " bytes of data..." << std::endl;
  return size * nmemb;
}

#if 0
size_t my_curl_physfs_write(void* ptr, size_t size, size_t nmemb, void* userdata)
{
  PHYSFS_file* f = static_cast<PHYSFS_file*>(userdata);
  PHYSFS_sint64 written = PHYSFS_writeBytes(f, ptr, size * nmemb);
  log_debug << "read " << size * nmemb << " bytes of data..." << std::endl;
  if (written < 0)
  {
    return 0;
  }
  else
  {
    return static_cast<size_t>(written);
  }
}
#endif

} // namespace

TransferStatus::TransferStatus(Downloader& downloader, TransferId id_,
                               const std::string& url) :
  m_downloader(downloader),
  id(id_),
  file(FileSystem::basename(url)),
  callbacks(),
  dltotal(0),
  dlnow(0),
  ultotal(0),
  ulnow(0),
  error_msg(),
  parent_list()
{}

void
TransferStatus::abort()
{
  m_downloader.abort(id);
}

void
TransferStatus::update()
{
  m_downloader.update();
}

TransferStatusList::TransferStatusList() :
  m_transfer_statuses(),
  m_successful_count(0),
  m_callbacks(),
  m_error_msg()
{
}

TransferStatusList::TransferStatusList(const std::vector<TransferStatusPtr>& list) :
  m_transfer_statuses(),
  m_successful_count(0),
  m_callbacks(),
  m_error_msg()
{
  for (TransferStatusPtr status : list)
  {
    push(status);
  }
}

void
TransferStatusList::abort()
{
  for (TransferStatusPtr status : m_transfer_statuses)
  {
    status->abort();
  }
  reset();
}

void
TransferStatusList::update()
{
  for (size_t i = 0; i < m_transfer_statuses.size(); i++)
  {
    m_transfer_statuses[i]->update();
  }
}

void
TransferStatusList::push(TransferStatusPtr status)
{
  assert(!status->parent_list);
  status->parent_list = this;

  m_transfer_statuses.push_back(status);
}

void
TransferStatusList::push(TransferStatusListPtr statuses)
{
  for (TransferStatusPtr status : statuses->m_transfer_statuses)
  {
    push(status);
  }
}

// Called when one of the transfers completes.
void
TransferStatusList::on_transfer_complete(TransferStatusPtr this_status, bool successful)
{
  if (successful)
  {
    m_successful_count++;
    if (m_successful_count == static_cast<int>(m_transfer_statuses.size()))
    {
      // All transfers have sucessfully completed.
      bool success = true;
      for (const auto& callback : m_callbacks)
      {
        try
        {
          callback(success);
        }
        catch (const std::exception& err)
        {
          success = false;
          log_warning << "Exception in Downloader: TransferStatusList callback failed: " << err.what() << std::endl;
          m_error_msg = err.what();
        }
      }

      reset();
    }
  }
  else
  {
    std::stringstream err;
    err << "Downloading \"" << this_status->file << "\" failed: " << this_status->error_msg;
    m_error_msg = err.str();
    log_warning << "Exception in Downloader: TransferStatusList: " << m_error_msg << std::endl;

    // Execute all callbacks.
    for (const auto& callback : m_callbacks)
    {
      callback(false);
    }

    reset();
  }
}

int
TransferStatusList::get_download_now() const
{
  int dlnow = 0;
  for (TransferStatusPtr status : m_transfer_statuses)
  {
    dlnow += status->dlnow;
  }
  return dlnow;
}

int
TransferStatusList::get_download_total() const
{
  int dltotal = 0;
  for (TransferStatusPtr status : m_transfer_statuses)
  {
    dltotal += status->dltotal;
  }
  return dltotal;
}

void
TransferStatusList::reset()
{
  m_transfer_statuses.clear();
  m_callbacks.clear();
  m_successful_count = 0;
}

bool
TransferStatusList::is_active() const
{
  return !m_transfer_statuses.empty();
}

class Transfer
{
protected:
  Downloader& m_downloader;
  TransferId m_id;

  const std::string m_url;

  TransferStatusPtr m_status;

public:
  Transfer(Downloader& downloader, TransferId id,
           const std::string& url) :
    m_downloader(downloader),
    m_id(id),
    m_url(url),
    m_status(new TransferStatus(m_downloader, id, url))
  {
  }

  virtual ~Transfer()
  {
  }

  TransferStatusPtr get_status() const
  {
    return m_status;
  }

  TransferId get_id() const
  {
    return m_id;
  }

  const std::string& get_url() const
  {
    return m_url;
  }

  virtual size_t on_data(const char* ptr, size_t size, size_t nmemb) = 0;

  int on_progress(double dltotal, double dlnow,
                   double ultotal, double ulnow)
  {
    m_status->dltotal = static_cast<int>(dltotal);
    m_status->dlnow = static_cast<int>(dlnow);

    m_status->ultotal = static_cast<int>(ultotal);
    m_status->ulnow = static_cast<int>(ulnow);

    return 0;
  }

private:
  Transfer(const Transfer&) = delete;
  Transfer& operator=(const Transfer&) = delete;
};

class FileTransfer final : public Transfer
{
public:
  FileTransfer(Downloader& downloader, TransferId id,
               const std::string& url, const std::string& outfile) :
    Transfer(downloader, id, url)
  {
  }

  size_t on_data(const char* ptr, size_t size, size_t nmemb) override
  {
    return size * nmemb;
  }

private:
  FileTransfer(const FileTransfer&) = delete;
  FileTransfer& operator=(const FileTransfer&) = delete;
};

class StringTransfer final : public Transfer
{
private:
  std::string& m_out;

public:
  StringTransfer(Downloader& downloader, TransferId id,
                 const std::string& url, std::string& outstr) :
    Transfer(downloader, id, url),
    m_out(outstr)
  {
  }

  size_t on_data(const char* ptr, size_t size, size_t nmemb) override
  {
    m_out += std::string(ptr, size * nmemb);
    return size * nmemb;
  }

private:
  StringTransfer(const StringTransfer&) = delete;
  StringTransfer& operator=(const StringTransfer&) = delete;
};

Downloader::Downloader() :
  m_transfers(),
  m_next_transfer_id(1),
  m_last_update_time(-1)
{
}

Downloader::~Downloader()
{
  m_transfers.clear();
}

void
Downloader::download(const std::string& url,
                     size_t (*write_func)(void* ptr, size_t size, size_t nmemb, void* userdata),
                     void* userdata)
{
  if (g_config->disable_network)
    throw std::runtime_error("Networking is disabled");

  log_info << "Downloading " << url << std::endl;

}

std::string
Downloader::download(const std::string& url)
{
  if (g_config->disable_network)
    throw std::runtime_error("Networking is disabled");

  std::string result;
  download(url, my_curl_string_append, &result);
  return result;
}

void
Downloader::download(const std::string& url, const std::string& filename)
{
  if (g_config->disable_network)
    throw std::runtime_error("Networking is disabled");
}

void
Downloader::abort(TransferId id)
{
  auto it = m_transfers.find(id);
  if (it == m_transfers.end())
  {
    log_warning << "transfer not found: " << id << std::endl;
  }
  else
  {
    TransferStatusPtr status = (it->second)->get_status();

    m_transfers.erase(it);

    for (const auto& callback : status->callbacks)
    {
      try
      {
        callback(false);
      }
      catch(const std::exception& err)
      {
        log_warning << "Illegal exception in Downloader: " << err.what() << std::endl;
      }
    }
    if (status->parent_list)
      status->parent_list->on_transfer_complete(status, false);
  }
}

void
Downloader::update()
{
  if (g_config->disable_network)
  {
    // Remove any on-going transfers
    for (const auto& transfer_data : m_transfers)
    {
      TransferStatusPtr status = transfer_data.second->get_status();
      status->error_msg = "Networking is disabled";
      for (const auto& callback : status->callbacks)
      {
        try
        {
          callback(false);
        }
        catch(const std::exception& err)
        {
          log_warning << "Illegal exception in Downloader: " << err.what() << std::endl;
        }
      }
      if (status->parent_list)
        status->parent_list->on_transfer_complete(status, false);
    }
    m_transfers.clear();
    return;
  }
}

TransferStatusPtr
Downloader::add_transfer(std::unique_ptr<Transfer> transfer)
{
  auto transferId = transfer->get_id();
  m_transfers[transferId] = std::move(transfer);
  return m_transfers[transferId]->get_status();
}

TransferStatusPtr
Downloader::request_download(const std::string& url, const std::string& filename)
{
  log_info << "Requesting download for: " << url << std::endl;
  return add_transfer(std::make_unique<FileTransfer>(*this, m_next_transfer_id++, url, filename));
}

TransferStatusPtr
Downloader::request_string_download(const std::string& url, std::string& out_string)
{
  log_info << "Requesting download for: " << url << std::endl;
  return add_transfer(std::make_unique<StringTransfer>(*this, m_next_transfer_id++, url, out_string));
}
