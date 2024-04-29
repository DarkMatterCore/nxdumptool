/*
 * file_writer.cpp
 *
 * Copyright (c) 2020-2024, DarkMatterCore <pabloacurielz@gmail.com>.
 *
 * This file is part of nxdumptool (https://github.com/DarkMatterCore/nxdumptool).
 *
 * nxdumptool is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * nxdumptool is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <file_writer.hpp>

namespace i18n = brls::i18n;    /* For getStr(). */
using namespace i18n::literals; /* For _i18n. */

namespace nxdt::utils
{
    FileWriter::FileWriter(const std::string& output_path, const size_t& total_size, const u32& nsp_header_size) : output_path(output_path), total_size(total_size), nsp_header_size(nsp_header_size)
    {
        /* Determine the storage device based on the input path. */
        this->storage_type = (this->output_path.starts_with(DEVOPTAB_SDMC_DEVICE) ? StorageType::SdCard  :
                             (this->output_path.starts_with('/')                  ? StorageType::UsbHost : StorageType::UmsDevice));

        if (this->storage_type != StorageType::UsbHost)
        {
            if (this->storage_type == StorageType::SdCard)
            {
                /* Always split big files if we're dealing with the SD card. */
                this->split_file = (this->total_size > FAT32_FILESIZE_LIMIT);
            } else {
                /* Get UMS device info. */
                UsbHsFsDevice ums_device{};
                if (!usbHsFsGetDeviceByPath(this->output_path.c_str(), &ums_device)) throw "utils/file_writer/ums_device_info_error"_i18n;

                /* Determine if we should split the output file based on the UMS device's filesystem type. */
                this->split_file = (this->total_size > FAT32_FILESIZE_LIMIT && ums_device.fs_type < UsbHsFsDeviceFileSystemType_exFAT);

                /* Calculate the number of part files we'll need, if applicable. */
                if (this->split_file) this->split_file_part_cnt = static_cast<u8>(ceil(static_cast<double>(this->total_size) / static_cast<double>(CONCATENATION_FILE_PART_SIZE)));
            }
        }

        /* Check free space. */
        if (auto chk = this->CheckFreeSpace()) throw *chk;

        /* Create initial file. */
        if (!this->CreateInitialFile())
        {
            this->Close(true);
            throw (this->storage_type == StorageType::UsbHost ? "utils/file_writer/initial_file/usb_host_error"_i18n : "utils/file_writer/initial_file/generic_error"_i18n);
        }

        /* Handle NSP header placeholder if a NSP header size was provided. */
        if (this->nsp_header_size)
        {
            if (this->storage_type != StorageType::UsbHost)
            {
                /* Write placeholder NSP header. */
                std::vector<u8> zeroes(static_cast<size_t>(this->nsp_header_size), 0);
                if (!this->Write(zeroes.data(), zeroes.size()))
                {
                    zeroes.clear();
                    this->Close(true);
                    throw "utils/file_writer/nsp_header_placeholder_error"_i18n;
                }
            }

            /* Manually adjust current file offset. */
            this->cur_size += this->nsp_header_size;
        }
    }

    FileWriter::~FileWriter()
    {
        this->Close();
    }

    std::optional<std::string> FileWriter::CheckFreeSpace(void)
    {
        u64 free_space = 0;

        /* Short-circuit: don't perform any free space check if we're dealing with a USB host or if the file size is zero. */
        if (this->storage_type == StorageType::UsbHost || !this->total_size) return {};

        /* Retrieve free space from the target storage device. */
        if (!utilsGetFileSystemStatsByPath(this->output_path.c_str(), nullptr, &free_space)) return "utils/file_writer/free_space_check/retrieve_error"_i18n;

        /* Perform the actual free space check. */
        bool ret = (free_space > this->total_size);
        if (!ret)
        {
            char needed_size_str[0x40] = {0};
            utilsGenerateFormattedSizeString(static_cast<double>(this->total_size), needed_size_str, sizeof(needed_size_str));
            return i18n::getStr("utils/file_writer/free_space_check/insufficient_space_error", needed_size_str);
        }

        return {};
    }

    void FileWriter::CloseCurrentFile(void)
    {
        if (this->fp)
        {
            fclose(this->fp);
            this->fp = nullptr;
        }
    }

    bool FileWriter::OpenNextFile(void)
    {
        /* Return immediately if: */
        /*     1. We're dealing with a USB host. */
        /*     2. We already created the initial file and we're dealing with the SD card. */
        /*     3. We already created the initial file and we're dealing with a UMS device and: */
        /*         3.1. We're not dealing with a split file. */
        /*         3.2. We already hit the total number of part files. */
        /*         3.3. The size for the current part file has not reached its limit. */
        if (this->storage_type == StorageType::UsbHost || (this->file_created && (this->storage_type == StorageType::SdCard ||
            (this->storage_type == StorageType::UmsDevice && (!this->split_file || this->split_file_part_idx >= this->split_file_part_cnt ||
            this->split_file_part_size < CONCATENATION_FILE_PART_SIZE))))) return true;

        /* Close current file. */
        this->CloseCurrentFile();

        if (this->storage_type == StorageType::SdCard || !this->split_file)
        {
            /* Open file using the provided output path, but only if we're dealing with the SD card or if we're not dealing with a split file. */
            this->fp = fopen(this->output_path.c_str(), "wb");
        } else {
            /* Open current part file. */
            std::string part_file_path = fmt::format("{}/{:02d}", this->output_path, this->split_file_part_idx);
            this->fp = fopen(part_file_path.c_str(), "wb");
            if (this->fp)
            {
                /* Update part file index. */
                this->split_file_part_idx++;

                /* Reset part file size. */
                this->split_file_part_size = 0;
            }
        }

        /* Return immediately if we couldn't open the file in creation mode. */
        if (!this->fp) return false;

        /* Close file and return immediately if we're dealing with an empty file. */
        if (!this->file_created && !this->total_size)
        {
            this->CloseCurrentFile();
            return true;
        }

        /* Disable file stream buffering. */
        setvbuf(this->fp, nullptr, _IONBF, 0);

        /* Truncate file to the right size. */
        off_t truncate_size = 0;

        if (this->storage_type == StorageType::SdCard || !this->split_file)
        {
            truncate_size = static_cast<off_t>(this->total_size);
        } else {
            if (this->split_file_part_idx < this->split_file_part_cnt)
            {
                truncate_size = static_cast<off_t>(CONCATENATION_FILE_PART_SIZE);
            } else {
                truncate_size = static_cast<off_t>(this->total_size - (CONCATENATION_FILE_PART_SIZE * static_cast<u64>(this->split_file_part_cnt - 1)));
            }
        }

        ftruncate(fileno(this->fp), truncate_size);

        return true;
    }

    bool FileWriter::CreateInitialFile(void)
    {
        /* Don't proceed if the file has already been created. */
        if (this->file_created) return true;

        const char *output_path_str = this->output_path.c_str();

        if (this->storage_type == StorageType::UsbHost)
        {
            /* Send file properties to USB host. */
            if ((!this->nsp_header_size && !usbSendFileProperties(this->total_size, output_path_str)) ||
                (this->nsp_header_size && !usbSendNspProperties(this->total_size, output_path_str, this->nsp_header_size))) return false;
        } else {
            /* Check if we're supposed to create a split file. */
            if (this->split_file)
            {
                if (this->storage_type == StorageType::SdCard)
                {
                    /* Create directory tree. */
                    utilsCreateDirectoryTree(output_path_str, false);

                    /* Create concatenation file on the SD card. */
                    if (!utilsCreateConcatenationFile(output_path_str)) return false;
                } else {
                    /* Create directory tree, including the last path element. */
                    /* We'll handle split file creation ourselves at a later point. */
                    utilsCreateDirectoryTree(output_path_str, true);
                }
            } else {
                /* Create directory tree. */
                utilsCreateDirectoryTree(output_path_str, false);
            }

            /* Open initial file. */
            if (!this->OpenNextFile()) return false;
        }

        /* Update flag. */
        this->file_created = true;

        return true;
    }

    bool FileWriter::Write(const void *data, const size_t& data_size)
    {
        /* Sanity check. */
        if (!data || !data_size || !this->file_created || this->cur_size >= this->total_size || (this->storage_type != StorageType::UsbHost && !this->fp)) return false;

        /* Make sure we don't write past the established file size. */
        size_t write_size = ((this->cur_size + data_size) > this->total_size ? (this->total_size - this->cur_size) : data_size);

        if (this->storage_type == StorageType::UmsDevice && this->split_file)
        {
            /* Switch to the next part file if we need to. */
            if (this->split_file_part_size >= CONCATENATION_FILE_PART_SIZE && !this->OpenNextFile()) return false;

            /* Make sure we don't write past the part file size limit. */
            size_t part_file_write_size = ((this->split_file_part_size + write_size) > CONCATENATION_FILE_PART_SIZE ? (CONCATENATION_FILE_PART_SIZE - this->split_file_part_size) : write_size);

            /* Write data to current part file. */
            if (fwrite(data, 1, part_file_write_size, this->fp) != part_file_write_size) return false;

            /* Update part file size. */
            this->split_file_part_size += part_file_write_size;

            /* Update the written data size. */
            this->cur_size += part_file_write_size;

            /* Write the rest of the data to the next part file if we need to. */
            if (part_file_write_size < write_size && !this->Write(static_cast<const u8*>(data) + part_file_write_size, write_size - part_file_write_size)) return false;
        } else {
            if (this->storage_type == StorageType::UsbHost)
            {
                /* Send data to USB host. */
                if (!usbSendFileData(data, write_size)) return false;
            } else {
                /* Write data to output file. */
                if (fwrite(data, 1, write_size, this->fp) != write_size) return false;
            }

            /* Update the written data size. */
            this->cur_size += write_size;
        }

        return true;
    }

    bool FileWriter::WriteNspHeader(const void *nsp_header, const u32& nsp_header_size)
    {
        /* Sanity check. */
        if (!nsp_header || !nsp_header_size || nsp_header_size != this->nsp_header_size || !this->file_created || this->cur_size < this->total_size || this->nsp_header_written ||
            (this->storage_type != StorageType::UsbHost && !this->fp)) return false;

        if (this->storage_type == StorageType::UmsDevice && this->split_file)
        {
            /* Close current file. */
            this->CloseCurrentFile();

            /* Open first part file. */
            std::string part_file_path = fmt::format("{}/00", this->output_path);
            this->fp = fopen(part_file_path.c_str(), "rb+");
            if (!this->fp) return false;
        }

        if (this->storage_type == StorageType::UsbHost)
        {
            /* Send NSP header to USB host. */
            if (!usbSendNspHeader(nsp_header, this->nsp_header_size)) return false;
        } else {
            /* Seek to the beginning of the file stream. */
            rewind(this->fp);

            /* Write NSP header. */
            if (fwrite(nsp_header, 1, this->nsp_header_size, this->fp) != this->nsp_header_size) return false;

            /* Close current file. */
            this->CloseCurrentFile();
        }

        /* Update flag. */
        this->nsp_header_written = true;

        return true;
    }

    void FileWriter::Close(bool force_delete)
    {
        /* Return immediately if the file has already been closed. */
        if (this->file_closed) return;

        /* Close current file. */
        this->CloseCurrentFile();

        /* Delete created file(s), if needed. */
        if (this->cur_size != this->total_size && (this->file_created || force_delete))
        {
            if (this->storage_type == StorageType::UsbHost)
            {
                usbCancelFileTransfer();
            } else {
                const char *output_path_str = this->output_path.c_str();

                if (this->storage_type == StorageType::SdCard)
                {
                    utilsRemoveConcatenationFile(output_path_str);
                } else {
                    if (this->split_file)
                    {
                        utilsDeleteDirectoryRecursively(output_path_str);
                    } else {
                        remove(output_path_str);
                    }
                }
            }
        }

        /* Commit SD card filesystem changes, if needed. */
        if (this->storage_type == StorageType::SdCard) utilsCommitSdCardFileSystemChanges();

        /* Update flag. */
        this->file_closed = true;
    }

    FileWriter::StorageType FileWriter::GetStorageType(void)
    {
        return this->storage_type;
    }
}
