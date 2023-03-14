/*
 * Open Surge Engine
 * quest.c - quest module
 * Copyright (C) 2008-2023  Alexandre Martins <alemartf@gmail.com>
 * http://opensurge2d.org
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "global.h"
#include "util.h"
#include "stringutil.h"
#include "logfile.h"
#include "quest.h"
#include "asset.h"
#include "nanoparser.h"



/* private stuff */
static int traverse_quest(const parsetree_statement_t* stmt, void *quest);
static bool has_extension(const char* filepath, const char* extension);
static quest_t* create_single_level_quest(quest_t* q, const char* path_to_lev_file);



/*
 * quest_load()
 * Loads the quest data from a file
 */
quest_t *quest_load(const char *filepath)
{
    quest_t* q = mallocx(sizeof *q);
    const char* fullpath;

    logfile_message("Loading quest \"%s\"...", filepath);
    fullpath = asset_path(filepath);

    /* default values */
    q->file = str_dup(filepath);
    q->name = str_dup("");
    q->author = str_dup("");
    q->version = str_dup("");
    q->description = str_dup("");
    q->level_count = 0;

    /* reading the quest */
    if(has_extension(filepath, ".qst")) {

        /* read quest file */
        parsetree_program_t* prog = nanoparser_construct_tree(fullpath);
        nanoparser_traverse_program_ex(prog, (void*)q, traverse_quest);
        nanoparser_deconstruct_tree(prog);

    }
    else if(has_extension(filepath, ".lev")) {

        /* implicitly create a quest with a single level */
        create_single_level_quest(q, filepath);

    }
    else {

        /* not a quest file */
        fatal_error("Can't load quest file \"%s\"", filepath);

    }

    /* success! */
    logfile_message("Quest \"%s\" has been loaded successfully!", q->name);
    return q;
}




/*
 * quest_unload()
 * Unload quest data
 */
quest_t *quest_unload(quest_t *qst)
{
    free(qst->file);
    free(qst->name);
    free(qst->author);
    free(qst->version);
    free(qst->description);

    for(int i = 0; i < qst->level_count; i++)
        free(qst->level_path[i]);

    free(qst);
    return NULL;
}




/* private functions */

/* interprets a statement from a .qst file */
int traverse_quest(const parsetree_statement_t* stmt, void *quest)
{
    quest_t *q = (quest_t*)quest;
    const char *id = nanoparser_get_identifier(stmt);
    const parsetree_parameter_t* param_list = nanoparser_get_parameter_list(stmt);
    const parsetree_parameter_t* p = nanoparser_get_nth_parameter(param_list, 1);

    if(str_icmp(id, "level") == 0) {
        nanoparser_expect_string(p, "Quest loader: expected level path");
        if(q->level_count < QUEST_MAXLEVELS)
            q->level_path[q->level_count++] = str_dup(nanoparser_get_string(p));
        else
            fatal_error("Quest loader: quests can't have more than %d levels", QUEST_MAXLEVELS);
    }
    else if(id[0] == '<' && id[strlen(id)-1] == '>') { /* special */
        if(q->level_count < QUEST_MAXLEVELS)
            q->level_path[q->level_count++] = str_dup(id);
        else
            fatal_error("Quest loader: quests can't have more than %d levels", QUEST_MAXLEVELS);
    }
    else if(str_icmp(id, "name") == 0) {
        nanoparser_expect_string(p, "Quest loader: quest name is expected");
        free(q->name);
        q->name = str_dup(nanoparser_get_string(p));
    }
    else if(str_icmp(id, "author") == 0) {
        nanoparser_expect_string(p, "Quest loader: quest author is expected");
        free(q->author);
        q->author = str_dup(nanoparser_get_string(p));
    }
    else if(str_icmp(id, "version") == 0) {
        nanoparser_expect_string(p, "Quest loader: quest version is expected");
        free(q->version);
        q->version = str_dup(nanoparser_get_string(p));
    }
    else if(str_icmp(id, "description") == 0) {
        /* make obsolete? will this field still have any use? */
        nanoparser_expect_string(p, "Quest loader: quest description is expected");
        free(q->description);
        q->description = str_dup(nanoparser_get_string(p));
    }
    else if(str_icmp(id, "image") == 0) {
        /* this field is obsolete and was removed
           this code is kept for retro-compatibility */
        nanoparser_expect_string(p, "Quest loader: quest image is expected");
        logfile_message("Quest loader: field image is obsolete");
    }
    else if(str_icmp(id, "hidden") == 0) {
        /* this field is obsolete and was removed
           this code is kept for retro-compatibility */
        logfile_message("Quest loader: field hidden is obsolete");
    }

    return 0;
}

/* create a quest structure with a single level (give a relative path to a .lev file) */
quest_t* create_single_level_quest(quest_t* q, const char* path_to_lev_file)
{
    free(q->file);
    free(q->name);

    q->file = str_dup(path_to_lev_file);
    q->name = str_dup(path_to_lev_file);

    q->level_path[0] = str_dup(path_to_lev_file);
    q->level_count = 1;

    return q;
}

/* checks if the provided filepath has the given extension (include the '.' at the extension) */
bool has_extension(const char* filepath, const char* extension)
{
    const char* ext = strrchr(filepath, '.');

    return (ext != NULL) && (0 == str_icmp(ext, extension));
}