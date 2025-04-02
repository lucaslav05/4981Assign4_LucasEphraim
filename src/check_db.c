//
// Created by lucas-laviolette on 4/1/25.
//

#include <stdio.h>
#include <ndbm.h>
#include <fcntl.h>
#include <string.h>

int main() {
    char key_str[] = "post_data";
    datum key = { key_str, (int)strlen(key_str) };

    DBM *db = dbm_open("../build/post_data", O_RDONLY, 0666);
    if (!db) {
        perror("Failed to open ndbm database");
        return 1;
    }

    datum value = dbm_fetch(db, key);
    if (value.dptr) {
        printf("Stored POST data: %.*s\n", value.dsize, value.dptr);
    } else {
        printf("No data found in database.\n");
    }

    dbm_close(db);
    return 0;
}
