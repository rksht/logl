#include "essentials.h"

using namespace fo;

#define POINTS_FILE SOURCE_DIR "/points.json"

#define ALLOCATOR memory_globals::default_allocator()

Array<Vector3> parse_points(const char *filename) {
    Array<uint8_t> source{ALLOCATOR};
    read_file(filename, source);

    nfcd_ConfigData *cd = simple_parse_file(filename, true);

    auto must = [](nfcd_loc loc, int line) {
        log_assert(loc != nfcd_null(), "nfcd parse failed Line: %i", line);
        return loc;
    };
#define MUST(loc) must(loc, __LINE__)

    auto root = nfcd_root(cd);

    auto points_loc = MUST(nfcd_object_lookup(cd, root, "points"));

    uint32_t num_points = nfcd_array_size(cd, points_loc);

    printf("Num points = %u\n", num_points);

    Array<Vector3> points{ALLOCATOR, num_points};

    for (uint32_t i = 0; i < num_points; ++i) {
        auto point_loc = MUST(nfcd_array_item(cd, points_loc, i));
        points[i].x = nfcd_to_number(cd, MUST(nfcd_array_item(cd, point_loc, 0)));
        points[i].y = nfcd_to_number(cd, MUST(nfcd_array_item(cd, point_loc, 1)));
        points[i].z = nfcd_to_number(cd, MUST(nfcd_array_item(cd, point_loc, 2)));
    }

    return points;
}

Array<Vector3> books_points() {
    Array<Vector3> points(ALLOCATOR);
    push_back(points, Vector3{-1, -2, 1});
    push_back(points, Vector3{1, 0, 2});
    push_back(points, Vector3{2, -1, 3});
    push_back(points, Vector3{2, -1, 2});
    return points;
}

int main() {
    memory_globals::init();
    {

        // auto points = parse_points(POINTS_FILE);
        auto points = books_points();
        auto pa = calculate_principal_axis(data(points), size(points));

        auto bb = create_bounding_rect(pa, data(points), size(points));

        printf(R"(
        EIGVALS:    [%.3f, %.3f, %.3f]

        EIGVECS:    {
                    [%.3f, %.3f, %.3f]
                    [%.3f, %.3f, %.3f]
                    [%.3f, %.3f, %.3f]
                    }

        Planes:     {
                    [R, %.3f]
                    [-R, %.3f]
                    [S, %.3f]
                    [-S, %.3f]
                    [T, %.3f]
                    [-T, %.3f]
                    }
    )",
               pa.eigvals[0], pa.eigvals[1], pa.eigvals[2], XYZ(pa.axes[0]), XYZ(pa.axes[1]), XYZ(pa.axes[2]),
               bb.planes[BoundingRect::R_POS].w, bb.planes[BoundingRect::R_NEG].w,
               bb.planes[BoundingRect::S_POS].w, bb.planes[BoundingRect::S_NEG].w,
               bb.planes[BoundingRect::T_POS].w, bb.planes[BoundingRect::T_NEG].w);
    }
    memory_globals::shutdown();
}
