function is_js_array(object) {
    return object.constructor === Array;
}

function shorten_string(s, length) {
    if (s.length <= length) {
        return s;
    }

    var dots = " ... ";

    var length_with_dots = length + dots.length;

    var a = new Array(length_with_dots);

    var side_size = Math.floor(length / 2);

    for (var i = 0; i < side_size; ++i) {
        a[i] = s[i];
    }

    for (var i = side_size; i < dots.length; ++i) {
        a[i] = dots[i - side_size];
    }

    for (
        var i = side_size + dots.length, j = s.length - 1;
        i < length_with_dots;
        ++i, --j
    ) {
        a[i] = s[j];
    }

    return a.join();
}

module.exports = {
    shorten_string
};
