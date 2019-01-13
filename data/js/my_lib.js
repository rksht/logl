function is_js_array(object) {
    return object.constructor === Array;
}

function shorten_string(s, length) {
    if (s.length <= length) {
        return s;
    }

    var dots = ' ... ';

    var length_with_dots = length + dots.length;

    var a = new Array(length_with_dots);

    var side_size = Math.floor(length / 2);

    for (var i = 0; i < side_size; ++i) {
        a[i] = s[i];
    }

    for (var i = side_size; i < dots.length; ++i) {
        a[i] = dots[i - side_size];
    }

    for (var i = side_size + dots.length, j = s.length - 1; i < length_with_dots; ++i, --j) {
        a[i] = s[j];
    }

    return a.join();
}

/*
// A custom function that creates a JSON object that doesn't print "large" things like a big array of floats.
function print_object_json_string: function(object, config) {
    var new_object = {};

    var keys = Object.getOwnPropertyNames(object);

    for (var i = 0; i < keys; ++i) {
        new_object[keys[i]] = null;
    }

    var to_delve = [];

    for (var i = 0; i < keys.length; ++i) {
        var key = keys[i];
        var value = object[keys[i]];

        if (typeof (value) === 'number') {
            new_object[key] = value;
        }

        else if (typeof (value) === 'string') {
            new_object[key] = shorten_string(value, 20);
        }

        else if (is_js_array(value)) {
            for (var j = 0; j < value.length; ++j) {
                to_delve.push(value[i]);
            }
        }

        else if (value.constructor === Float32Array || value.constructor === Uint8Array) {

        }

        else if (typeof(value) == 'object') {
            to_delve.push(value);
        }
    }
}
*/

module.exports = {
    shorten_string
};
