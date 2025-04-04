//
// Created by lucas-laviolette on 3/30/25.
//

#ifndef LIBHTTP_H
#define LIBHTTP_H

#include <ndbm.h>

void handle_request(int client_fd, DBM *db);
const char* get_mime_type(const char *file_ext);
void serve_file(int client_fd, const char *file_path);

#endif //LIBHTTP_H
