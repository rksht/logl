function Vector3(x, y, z) {
    this._a = new Float32Array(3);
    this._a[0] = x;
    this._a[1] = y;
    this._a[2] = z;
};

Vector3.prototype.x = function() {
    return this._a[0];
};

Vector3.prototype.y = function() {
    return this._a[1];
};

Vector3.prototype.x = function() {
    return this._a[2];
};

Vector3.prototype.set_from = function(v) {
    this._a[0] = v._a[0];
    this._a[1] = v._a[1];
    this._a[2] = v._a[2];
};

function Noise2D(x_size, y_size) {
    this.x_size = x_size;
    this.y_size = y_size;
    this.unit_texel_size_x = 1.0 / x_size;
    this.unit_texel_size_y = 1.0 / y_size;

    this.resolution = x_size < y_size ? x_size : y_size;

    this.array = new Float32Array(x_size * y_size);
}

Noise2D.prototype.simple = function(point) {
};
