#include <blend2d.h>

#include <learnogl/eng.h>
#include <learnogl/kitchen_sink.h>

using namespace eng::math;

/////////////////////////////////////////////////////////////////////////////////////////////////////

constexpr f64 SCALE_DIST = 1.0;
constexpr i32 MAX_STEPS = 100000;
constexpr f64 MIN_GRADIENT_LENGTH = 0.00000001;
constexpr f64 STEP_SIZE = 0.05;

constexpr i32 IMAGE_WIDTH = 1024;
constexpr i32 IMAGE_HEIGHT = 768;

f64 cos_distance(Vec2 p) { return -std::cos(magnitude(p) / SCALE_DIST) + 1; }

Vec2 grad_cos_distance(Vec2 p)
{
	const_ dist = square_magnitude(p);
	return (std::sin(dist) / (dist * SCALE_DIST)) * p;
}

constexpr Vec2 get_starting_point() { return Vec2(1.0, 1.0); }

struct StepResult
{
	Vec2 updated_point;
	bool should_stop;
};

StepResult step(Vec2 cur_point, i32 index)
{

	Vec2 grad = grad_cos_distance(cur_point);
	var_ step_diff = STEP_SIZE * grad;
	var_ step_len = magnitude(step_diff);

	if (step_len < MIN_GRADIENT_LENGTH)
	{
		LOG_F(INFO, "step_len(=%.5f) < MIN_STEP_ALLOWED - Stopping", step_len);
		return StepResult{ cur_point, true };
	}

	return StepResult{ cur_point - step_diff, false };
}

constexpr f64 PIXELS_PER_WORLD_UNIT_X = 2.0;
constexpr f64 PIXELS_PER_WORLD_UNIT_Y = 500.0;

struct DrawContext
{
	BLImage image_target;
	BLContext bl_context;

	Vec2 origin_pixel;

	DrawContext()
	    : image_target(IMAGE_WIDTH, IMAGE_HEIGHT, BL_FORMAT_PRGB32)
	    , bl_context(image_target)
	{
		bl_context.setCompOp(BL_COMP_OP_SRC_COPY);
		bl_context.fillAll();

		origin_pixel = Vec2(100.0f, 10.0f);
	}

	BLPath start_graph()
	{
		var_ path = BLPath();
		path.moveTo(origin_pixel.x, origin_pixel.y);
		return path;
	}

	Vec2 pixel_position(i32 iter_number, f32 distance)
	{
		f32 ypos_pixel = distance * PIXELS_PER_WORLD_UNIT_Y;
		f32 xpos_pixel = f32(iter_number) * PIXELS_PER_WORLD_UNIT_X;
		Vec2 pixel_pos = origin_pixel + Vec2(xpos_pixel, ypos_pixel);
		return Vec2(std::floor(pixel_pos.x), std::floor(pixel_pos.y));
	}

	void add_point(BLPath &path, i32 iter_number, Vec2 cur_point)
	{
		f64 dist_from_zero = magnitude(cur_point);
		Vec2 p = pixel_position(iter_number, dist_from_zero);
		path.lineTo(i32(p.x), i32(p.y));

		LOG_F(INFO, "LineTo: [%.0f, %.0f]", XY(p));
	}

	void done_adding_points(BLPath &path)
	{
		BLGradient linear(BLLinearGradientValues(0, 0, 0, 480));
		linear.addStop(0.0, BLRgba32(0xFFFFFFFF));
		linear.addStop(1.0, BLRgba32(0xFF00FFFF));
		bl_context.setCompOp(BL_COMP_OP_SRC_OVER);
		bl_context.setStrokeStyle(linear);
		bl_context.setStrokeWidth(10);
		bl_context.setStrokeStartCap(BL_STROKE_CAP_ROUND);
		bl_context.setStrokeEndCap(BL_STROKE_CAP_BUTT);
		bl_context.strokePath(path);
		bl_context.end();
	}

	void write_image()
	{
		LOG_F(INFO, "write_image");
		BLImageCodec codec;
		codec.findByName("BMP");
		image_target.writeToFile("cos_distance_grad_descent.bmp", codec);
	}
};

Vec2 do_gradient_descent()
{
	DrawContext draw_context;

	var_ path = draw_context.start_graph();

	var_ cur_point = get_starting_point();

	i32 i = 0;
	for (; i < MAX_STEPS; i++)
	{
		if (i % 100 == 0)
		{
			LOG_F(INFO, "Iter %i", i);
		}
		var_ step_result = step(cur_point, i);

		cur_point = step_result.updated_point;

		draw_context.add_point(path, i, cur_point);

		if (step_result.should_stop)
		{
			break;
		}
	}

	draw_context.done_adding_points(path);
	draw_context.write_image();

	LOG_IF_F(INFO, i == MAX_STEPS, "Did not reach a minimum in %i steps", MAX_STEPS);

	return cur_point;
}

int main()
{
	eng::init_memory();
	DEFERSTAT(eng::shutdown_memory());

	Vec2 minimum = do_gradient_descent();

	LOG_F(INFO, "Minimum point = [%.9f, %.9f]", XY(minimum));
}
