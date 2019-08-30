const type = require("type_check");

function my_name_is_alice() {
	return "Alice";
}

module.exports = {
	my_name_is_alice: my_name_is_alice
};

const InputLayoutSpec = new type.RecordSpec({
	position: new type.StringSpec(["vec2", "vec3", "vec4"]),
	normal: new type.StringSpec(["vec3"]),
	uv: new type.StringSpec(["vec2"])
});

const BindingSpec = new type.RecordSpec({
	bindpoint: new type.NumberSpec(0, 10),
	format: new type.StringSpec(["std140", "std430"]),
	is_uniform: new type.BooleanSpec()
});

type.check({ bindpoint: 9, format: "std140", is_uniform: true }, BindingSpec);
