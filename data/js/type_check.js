// State keeps a list of all already seen objects since there can be reference cycles.
function TypeCheckState() {
	this.touched_objects = [];
}

TypeCheckState.prototype.touch = function(object) {
	for (var i = 0; i < this.touched_objects.length; ++i) {
		if (object === this.touched_objects[i]) {
			return true;
		}
	}
	this.touched_objects.push(object);
	return false;
};

TypeCheckState.prototype.add_newly_seen_object = function(object) {
	this.wip_objects.push(object);
};

// extra_opts is optional, can contain { length, max_length }
function ArraySpec(element_type, extra_opts) {
	if (!extra_opts) {
		extra_opts = {};
	}

	this.element_type = element_type;
	this.extra_opts = extra_opts;
}

ArraySpec.prototype.type_check = function(type_check_state, value) {
	if (type_check_state.touch(value)) {
		return;
	}

	type_check_state.add_newly_seen_object(value);

	if (!Array.isArray(value)) {
		throw "Value - " + value + " is not an array";
	}

	for (var i = 0; i < value.length; i++) {
		this.element_type.type_check(type_check_state, value[i]);
	}

	if (this.extra_opts) {
		if (
			Number.isInteger(this.extra_opts.length) &&
			this.extra_opts.length !== value.length
		) {
			throw "Value " +
				value +
				" should have length " +
				this.extra_opts.length;
		}

		if (
			Number.isInteger(this.extra_opts.max_length) &&
			this.extra_opts.max_length < value.length
		) {
			throw "Value " +
				value +
				" should have length " +
				this.extra_opts.length;
		}
	}
};

function RecordSpec(type_spec) {
	this.type_spec = type_spec;
}

RecordSpec.prototype.type_check = function(type_check_state, object) {
	if (type_check_state.touch(object)) {
		console.log("Already seen...", object);
		return;
	}

	console.log("Checking type of ", object);

	if (Array.isArray(typeof object) || typeof object === "function") {
		throw "Value is not an object";
	}

	for (var key in object) {
		if (!object.hasOwnProperty(key)) {
			continue;
		}

		if (!this.type_spec.hasOwnProperty(key)) {
			continue;
		}

		this.type_spec[key].type_check(type_check_state, object[key]);
	}
};

function StringSpec(set) {
	this.set = set;
}

StringSpec.prototype.type_check = function(type_check_state, value) {
	if (type_check_state.touch(value)) {
		return;
	}

	if (typeof value !== "string") {
		throw "Value " + value + " is not a string";
	}

	if (!this.set || this.set.length === 0) {
		return;
	}

	for (var i = 0; i < this.set.length; ++i) {
		if (this.set[i] === value) {
			return;
		}
	}

	throw "Value must be one of " + this.set.join(", ");
};

function NumberSpec(range_min, range_max, is_integer) {
	this.range_min = typeof range_min === "number" ? range_min : null;
	this.range_max = typeof range_max === "number" ? range_max : null;
	this.is_integer = is_integer;
}

NumberSpec.prototype.type_check = function(type_check_state, value) {
	if (type_check_state.touch(value)) {
		return;
	}

	if (this.is_integer) {
		if (!Number.isInteger(value)) {
			throw "Value " + value + " is not a number";
		}
	} else {
		if (!typeof value === "number") {
			throw "Value " + value + " is not a number";
		}
	}

	if (this.range_min !== null && this.range_max !== null) {
		if (!(this.range_min <= value && value < this.range_max)) {
			throw "Value " +
				value +
				" must be between " +
				this.range_min +
				" and " +
				this.range_max;
		}
	}
};

function BooleanSpec() {}

BooleanSpec.prototype.type_check = function(type_check_state, value) {
	if (typeof value !== "boolean") {
		throw "Value " + value + " is not a boolean";
	}
};

function check(value, spec, dont_throw_exception) {
	const s = new TypeCheckState();

	try {
		spec.type_check(s, value);
		return { error: null };
	} catch (e) {
		if (dont_throw_exception) {
			return { error: e };
		}

		throw e;
	}
}

module.exports = {
	check,
	ArraySpec,
	RecordSpec,
	NumberSpec,
	StringSpec,
	BooleanSpec
};
