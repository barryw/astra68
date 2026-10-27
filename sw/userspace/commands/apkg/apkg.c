#include <astra/package_library.h>
#include <astra/posix.h>
#include <astra/program.h>
#include <astra/vfs_process.h>

#include <stdio.h>
#include <string.h>

ASTRA_PROGRAM("apkg", 0, 1, 0, "Astra68 contributors",
              "Copyright 2026 Astra68 contributors");

int main(int argc, char **argv)
{
    AstraProcessFilesystem filesystem = ASTRA_PROCESS_FILESYSTEM_INIT;
    if (argc != 4 || (strcmp(argv[1], "create") != 0 &&
                      strcmp(argv[1], "install") != 0)) {
        fputs("usage: apkg create BUNDLE.app OUTPUT.apkg\n"
              "       apkg install ARCHIVE.apkg APPLICATION-DIRECTORY\n", stderr);
        return 2;
    }
    uint32_t opened = astra_process_filesystem_open(&filesystem,
                                                     astra_posix_startup());
    if (opened != ASTRA_VFS_OK) {
        fprintf(stderr, "apkg: filesystem unavailable (%u)\n", opened);
        return 1;
    }
    int create = strcmp(argv[1], "create") == 0;
    AstraResult result = create ?
        astra_package_create_bundle(&filesystem.filesystem, argv[2], argv[3]) :
        astra_package_install_bundle(&filesystem.filesystem, argv[2], argv[3]);
    astra_process_filesystem_close(&filesystem);
    if (result != ASTRA_OK) {
        fprintf(stderr, "apkg: could not %s %s (%ld)\n",
                argv[1], create ? argv[3] : argv[2], (long)result);
        return 1;
    }
    if (create) printf("Created %s\n", argv[3]);
    else printf("Installed %s into %s\n", argv[2], argv[3]);
    return 0;
}
