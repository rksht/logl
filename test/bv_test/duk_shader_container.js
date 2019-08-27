// Trying out how I would expressively want to build a shader program.

var DIM_1D = 0;
var DIM_2D = 1;
var DIM_3D = 2;

var LAYOUT_STD140 = "std140";
var LAYOUT_STD430 = "std430";

var SHADER_KIND_VERTEX = 0;
var SHADER_KIND_FRAGMENT = 1;

var SHADER_RESOURCE_TEXTURE = 0;
var SHADER_RESOURCE_STORAGE_BUFFER = 1;
var SHADER_RESOURCE_UNIFORM_BUFFER = 2;
var SHADER_RESOURCE_ATOMIC_COUNTER_BUFFER = 3;

function make_sampler_info(dims, type, name) {
	return { dims: dims, type: type, name: name };
}

function BlockInfo(bindpoint, layout_format) {
	this.bindpoint = bindpoint;
	this.layout_format = layout_format;
}

function Member(type_name, member_name) {
	this.type_name = type_name;
	this.member_name = member_name;
}

function BufferBlock(
	type,
	name,
	bindpoint,
	layout_format,
	members,
	readwrite_access
) {
	this.type = type;

	this.name = name;
	this.bindpoint = bindpoint;
	this.layout_format = layout_format;
	this.members = members;

	if (
		readwrite_access !== "readonly" &&
		readwrite_access !== "readonly" &&
		readwrite_access !== "writeonly"
	) {
		throw "Invalid readwrite_access specified - " +
			readwrite_access;
	}

	this.readwrite_access = readwrite_access;
}

BufferBlock.prototype.stringify = function() {
	/*
		layout(binding = 0, std140)
	*/

	var tokens = [
		"layout(binding =",
		this.bindpoint,
		",",
		this.layout_format,
		")"
	];

	if (this.type === SHADER_RESOURCE_STORAGE_BUFFER) {
		tokens.push("buffer");
	} else if (this.type === SHADER_RESOURCE_UNIFORM_BUFFER) {
		tokens.push("uniform");
	} else {
		throw "Invalid buffer block type - " + this.type;
	}

	this.tokens.push("{", "\n");

	for (var i = 0; i < this.members.length; ++i) {
		var member = this.members[i];
		this.tokens.push(member.type_name, member.name, ";", "\n");
	}

	this.tokens.push("}");
};

function ShaderBuilder(shader_include_paths) {
	this.shader_include_paths = !!shader_include_paths
		? shader_include_paths
		: [];
	this.vertex_shader = null;
	this.fragment_shader = null;

	this.uniform_blocks_by_name = {};
	this.storage_blocks_by_name = {};
	this.texture_samplers_by_name = {};
}

ShaderBuilder.prototype.add_vertex_shader = function(vs_source, debug_name) {
	if (!vs_source) {
		throw "Expected source to be a non-empty string";
	}

	if (!debug_name) {
		debug_name = "unnamed";
	}
};

ShaderBuilder.prototype.include_file = function(relative_file_path) {
	for (var i = 0; i < this.shader_include_paths; ++i) {}
};

ShaderBuilder.prototype.add_fragment_shader = function(
	fs_source,
	debug_name
) {};
