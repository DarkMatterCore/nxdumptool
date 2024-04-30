/*
 * fs_ext.c
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

#include <core/nxdt_utils.h>
#include <core/fs_ext.h>

/* IFileSystemProxy. */
Result fsOpenGameCardStorage(FsStorage *out, const FsGameCardHandle *handle, u32 partition)
{
    const struct {
        FsGameCardHandle handle;
        u32 partition;
    } in = { *handle, partition };

    return serviceDispatchIn(fsGetServiceSession(), 30, in,
        .out_num_objects = 1,
        .out_objects = &out->s
    );
}

Result fsOpenGameCardDetectionEventNotifier(FsEventNotifier *out)
{
    return serviceDispatch(fsGetServiceSession(), 501,
        .out_num_objects = 1,
        .out_objects = &out->s
    );
}

/* IDeviceOperator. */
Result fsDeviceOperatorUpdatePartitionInfo(FsDeviceOperator *d, const FsGameCardHandle *handle, u32 *out_title_version, u64 *out_title_id)
{
    const struct {
        FsGameCardHandle handle;
    } in = { *handle };

    struct {
        u32 title_version;
        u64 title_id;
    } out;

    Result rc = serviceDispatchInOut(&d->s, 203, in, out);

    if (R_SUCCEEDED(rc) && out_title_version) *out_title_version = out.title_version;
    if (R_SUCCEEDED(rc) && out_title_id) *out_title_id = out.title_id;

    return rc;
}
