function fill_array(o, n)
{
    var a = new Array(n);
    for (var i = 0; i < n; ++i) {
        a[i] = o;
    }
    return a;
}

function fill_array_with_fn(fn, n)
{
    var a = new Array(n);
    for (var i = 0; i < n; ++i) {
        a[i] = fn();
    }
    return a;
}

function PascalsTriangle(n_last)
{
    console.log("Constructing a pascal's triangle with " + (n_last + 1) + "rows");

    this.row_length = n_last + 1;
    this.table = fill_array(0, this.row_length * this.row_length);

    for (var n = 0; n < this.row_length; ++n) {
        this.table[n * this.row_length] = 1;
    }

    for (var n = 1; n < this.row_length; ++n) {
        for (var k = 1; k < this.row_length; ++k) {
            this.table[n * this.row_length + k] =
                this.table[(n - 1) * this.row_length + k] + this.table[(n - 1) * this.row_length + (k - 1)];
        }
    }

    this.get = function(n, k) { return this.table[n * this.row_length + k]; };

    this.row_begin = function(r) { return r * this.row_length; };

    this.row_end = function(r) { return this.row_begin(r + 1); }
}

/*
var pt = new PascalsTriangle(9);

for (var i = pt.row_begin(9), i_end = pt.row_end(9); i < i_end; ++i) {
    console.log(pt.table[i]);
}
*/
