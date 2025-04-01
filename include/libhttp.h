//
// Created by lucas-laviolette on 3/30/25.
//

#ifndef LIBHTTP_H
#define LIBHTTP_H

#include <ndbm.h>

void handle_request(int client_fd, DBM *db);

#endif //LIBHTTP_H
