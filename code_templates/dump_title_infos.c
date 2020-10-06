    if (g_titleInfo && g_titleInfoCount)
    {
        mkdir("sdmc:/records", 0777);
        
        FILE *title_infos_txt = NULL, *icon_jpg = NULL;
        char icon_path[FS_MAX_PATH] = {0};
        
        title_infos_txt = fopen("sdmc:/records/title_infos.txt", "wb");
        if (title_infos_txt)
        {
            for(u32 i = 0; i < g_titleInfoCount; i++)
            {
                fprintf(title_infos_txt, "Storage ID: 0x%02X\r\n", g_titleInfo[i].storage_id);
                fprintf(title_infos_txt, "Title ID: %016lX\r\n", g_titleInfo[i].meta_key.id);
                fprintf(title_infos_txt, "Version: %u (%u.%u.%u-%u.%u)\r\n", g_titleInfo[i].meta_key.version, g_titleInfo[i].version.TitleVersion_Major, \
                        g_titleInfo[i].version.TitleVersion_Minor, g_titleInfo[i].version.TitleVersion_Micro, g_titleInfo[i].version.TitleVersion_MajorRelstep, \
                        g_titleInfo[i].version.TitleVersion_MinorRelstep);
                fprintf(title_infos_txt, "Type: 0x%02X\r\n", g_titleInfo[i].meta_key.type);
                fprintf(title_infos_txt, "Install Type: 0x%02X\r\n", g_titleInfo[i].meta_key.install_type);
                fprintf(title_infos_txt, "Title Size: %s (0x%lX)\r\n", g_titleInfo[i].title_size_str, g_titleInfo[i].title_size);
                
                fprintf(title_infos_txt, "Content Count: %u\r\n", g_titleInfo[i].content_count);
                for(u32 j = 0; j < g_titleInfo[i].content_count; j++)
                {
                    char content_id_str[SHA256_HASH_SIZE + 1] = {0};
                    utilsGenerateHexStringFromData(content_id_str, sizeof(content_id_str), g_titleInfo[i].content_infos[j].content_id.c, SHA256_HASH_SIZE / 2);
                    
                    u64 content_size = 0;
                    titleConvertNcmContentSizeToU64(g_titleInfo[i].content_infos[j].size, &content_size);
                    
                    char content_size_str[32] = {0};
                    utilsGenerateFormattedSizeString(content_size, content_size_str, sizeof(content_size_str));
                    
                    fprintf(title_infos_txt, "    Content #%u:\r\n", j + 1);
                    fprintf(title_infos_txt, "        Content ID: %s\r\n", content_id_str);
                    fprintf(title_infos_txt, "        Content Size: %s (0x%lX)\r\n", content_size_str, content_size);
                    fprintf(title_infos_txt, "        Content Type: 0x%02X\r\n", g_titleInfo[i].content_infos[j].content_type);
                    fprintf(title_infos_txt, "        ID Offset: 0x%02X\r\n", g_titleInfo[i].content_infos[j].id_offset);
                }
                
                if ((g_titleInfo[i].meta_key.type == NcmContentMetaType_Application && g_titleInfo[i].app_metadata) || ((g_titleInfo[i].meta_key.type == NcmContentMetaType_Patch || \
                    g_titleInfo[i].meta_key.type == NcmContentMetaType_AddOnContent) && g_titleInfo[i].parent && g_titleInfo[i].parent->app_metadata))
                {
                    TitleApplicationMetadata *app_metadata = (g_titleInfo[i].meta_key.type == NcmContentMetaType_Application ? g_titleInfo[i].app_metadata : g_titleInfo[i].parent->app_metadata);
                    
                    if (strlen(app_metadata->lang_entry.name)) fprintf(title_infos_txt, "Name: %s\r\n", app_metadata->lang_entry.name);
                    if (strlen(app_metadata->lang_entry.author)) fprintf(title_infos_txt, "Author: %s\r\n", app_metadata->lang_entry.author);
                    
                    if (g_titleInfo[i].meta_key.type == NcmContentMetaType_Application && app_metadata->icon_size && app_metadata->icon)
                    {
                        fprintf(title_infos_txt, "JPEG Icon Size: 0x%X\r\n", app_metadata->icon_size);
                        
                        sprintf(icon_path, "sdmc:/records/%016lX.jpg", app_metadata->title_id);
                        
                        icon_jpg = fopen(icon_path, "wb");
                        if (icon_jpg)
                        {
                            fwrite(app_metadata->icon, 1, app_metadata->icon_size, icon_jpg);
                            fclose(icon_jpg);
                            icon_jpg = NULL;
                            utilsCommitSdCardFileSystemChanges();
                        }
                    }
                }
                
                if (g_titleInfo[i].meta_key.type == NcmContentMetaType_Patch || g_titleInfo[i].meta_key.type == NcmContentMetaType_AddOnContent)
                {
                    if (g_titleInfo[i].previous) fprintf(title_infos_txt, "Previous %s ID: %016lX\r\n", g_titleInfo[i].meta_key.type == NcmContentMetaType_Patch ? "Patch" : "AOC", g_titleInfo[i].previous->meta_key.id);
                    if (g_titleInfo[i].next) fprintf(title_infos_txt, "Next %s ID: %016lX\r\n", g_titleInfo[i].meta_key.type == NcmContentMetaType_Patch ? "Patch" : "AOC", g_titleInfo[i].next->meta_key.id);
                }
                
                fprintf(title_infos_txt, "\r\n");
                
                fflush(title_infos_txt);
            }
            
            fclose(title_infos_txt);
            title_infos_txt = NULL;
            utilsCommitSdCardFileSystemChanges();
        }
    }