#define JNI_VER_MAJOR	0
#define JNI_VER_MINOR	5
#define JNI_VER_MICRO	0
#define JNI_REVISION	"dev build"
#define JNI_VER_TAG        "r-1"
#define JNI_VER_NUMTAG     "+"

#define JNI_VER_NUM        APR_STRINGIFY(JNI_VER_MAJOR) \
                           "." APR_STRINGIFY(JNI_VER_MINOR) \
                           "." APR_STRINGIFY(JNI_VER_MICRO)

/** Version number with tag (contains no whitespace) */
#define JNI_VER_NUMBER     JNI_VER_NUM JNI_VER_NUMTAG

/** Complete version string */
#define JNI_VERSION        JNI_VER_NUM " (" JNI_VER_TAG ")"
