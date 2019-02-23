#define STB_RECT_PACK_IMPLEMENTATION
#define STB_TRUETYPE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION

#include <learnogl/essential_headers.h>

#include <learnogl/stb_rect_pack.h>

#include <learnogl/stb_truetype.h>

#include <learnogl/stb_image_write.h>

#include <learnogl/stb_image.h>

#if defined(USING_MAPBOX_VARIANT) || defined(USING_MAPBOX_OPTIONAL)
#    if defined(_MSC_VER)
#        pragma message("Not really preproc warning... Using mapbox optional and/or variant ")
#    else
#        warning "Not really preproc warning... Using mapbox optional and/or variant"
#    endif
#endif
