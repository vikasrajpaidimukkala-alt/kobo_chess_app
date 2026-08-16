#include <stdio.h>

#include "fbink.h"

int main(void)
{
    FBInkConfig cfg = {0};

    int fbfd = fbink_open();

    if (fbfd < 0) {
        fprintf(stderr, "fbink_open() failed: %d\n", fbfd);
        return 1;
    }

    int rc = fbink_init(fbfd, &cfg);

    if (rc < 0) {
        fprintf(stderr, "fbink_init() failed: %d\n", rc);
        fbink_close(fbfd);
        return 1;
    }

    printf("Framebuffer opened and initialized!\n");

    rc = fbink_print(
        fbfd,
        "KOBO CHESS\n\nFBInk is working!",
        &cfg
    );

    if (rc < 0) {
        fprintf(stderr, "fbink_print() failed: %d\n", rc);
        fbink_close(fbfd);
        return 1;
    }

    printf("Text rendered successfully!\n");

    fbink_close(fbfd);

    return 0;
}
