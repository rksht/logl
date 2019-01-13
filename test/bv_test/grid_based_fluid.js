// Creates a new grid with x_count and y_count cells along the x and y directions.
function Grid(x_count, y_count, name)
{
    this.name = name;
    this.x_count = x_count;
    this.y_count = y_count;
    this.array = new Float32Array(x_count * y_count);

    console.log("Made a new grid - " + this.name + "size = " + x_count + " x " + y_count);
}

Grid.prototype.num_cells = function() {
    return this.x_count * this.y_count;
};

Grid.prototype.at = function(x, y) {
    return this.array[x + this.x_count * y];
};

Grid.prototype.set = function(x, y, value) {
    this.array[x + this.x_count * y] = value;
};

Grid.prototype.fill = function(value) {
    var length = this.array.length;
    for (var i = 0; i < length; ++i) {
        this.array[i] = value;
    }
    console.log("Filled the grid with " + value);
    console.log("Grid type = " + typeof (this.array));
};

Grid.prototype.console_print = function(x, y) {
    console.log('[' + x + ', ' + y + '] = ' + this.at(x, y));
};

console.log("File loaded");

var GridUV = function(x_count, y_count) {
    this.u = new Grid(x_count, y_count);
    this.v = new Grid(x_count, y_count);
};

function Simulation(x_count, y_count, timestep_size, cell_size)
{

    // Reference to the grids which will be used for current update
    this.state_0 = new GridUV(x_count, y_count);

    // Reference to the grids which will be updated
    this.state_1 = new GridUV(x_count, y_count);

    // Time step in second.
    this.dt = timestep_size;

    this.cell_size = cell_size;

    this.x_count = x_count;
    this.y_count = y_count;
}


// Stam's first simulation. Only density being advected through a fixed velocity field.

// The first thing we do is add density due to sources. The source grid is a scalar grid that contains source-
// per-second at each cell.
Simulation.prototype.add_sources = function(source_grid, dest_grid) {
    for (var j = 0; j < this.y_count; ++j) {
        for (var i = 0; i < this.x_count; ++i) {
            var current = source_grid.at(i, j);
            dest_grid.set(i, j, current + this.dt * source_grid.at(i, j));
        }
    }
};

const NUM_ITERATIONS = 20;

Simulation.prototype.diffuse_density = function(source_grid, dest_grid) {
    for (var k = 0; k < 20; ++k) {
        for (var i = 0; i < this.x_count; ++i) {
            for (var j = 0; j < this.y_count; ++i) {

                dest_grid.set(i, j)
            }
        }
    }
};
