//
// Created by lucas-laviolette on 4/1/25.
//

#include <stdio.h>
#include <ndbm.h>
#include <fcntl.h>
#include <string.h>

int main() {
    DBM *db = dbm_open("../build/post_data", O_RDONLY, 0666);
    if (!db) {
        perror("Failed to open ndbm database");
        return 1;
    }

    datum key = dbm_firstkey(db);
    while (key.dptr != NULL) {
        datum value = dbm_fetch(db, key);
        if (value.dptr) {
            printf("Key: %.*s\n", (int)key.dsize, (char *)key.dptr);
            printf("Value: %.*s\n\n", (int)value.dsize, (char *)value.dptr);
        }

        key = dbm_nextkey(db);
    }

    dbm_close(db);
    return 0;
}