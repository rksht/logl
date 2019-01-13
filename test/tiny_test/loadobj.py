import ctypes as C
import numpy as np

LOAD_OBJ = None

MEMORY_GLOBALS_INIT = False

def init(path_to_dll):
    global LOAD_OBJ

    LOAD_OBJ = C.CDLL(path_to_dll)

    LOAD_OBJ.create_model.argtypes = []
    LOAD_OBJ.create_model.restype = C.c_void_p
    
    LOAD_OBJ.load_model.argtypes = [C.c_void_p, C.c_char_p, C.c_uint32]
    LOAD_OBJ.load_model.restype = C.c_int

    LOAD_OBJ.delete_model.argtypes = [C.c_void_p]

    LOAD_OBJ.num_meshes_in_model.argtypes = [C.c_void_p]
    LOAD_OBJ.num_meshes_in_model.restype = C.c_uint32

    LOAD_OBJ.get_mesh_data.argtypes = [C.c_void_p, C.c_uint32]
    LOAD_OBJ.get_mesh_data.restype = C.c_void_p

    LOAD_OBJ.get_mesh_data.argtypes = [C.c_void_p]
    LOAD_OBJ.get_mesh_data.restype = C.POINTER(C.c_ushort)

    LOAD_OBJ.get_indices.argtypes = [C.c_void_p]
    LOAD_OBJ.get_indices.restype = C.POINTER(C.c_ushort)

    LOAD_OBJ.get_buffer.argtypes = [C.c_void_p]
    LOAD_OBJ.get_buffer.restype = C.POINTER(C.c_uint8)

    LOAD_OBJ.get_position.argtypes = [C.c_void_p, C.c_uint32, C.POINTER(C.c_float)]
    LOAD_OBJ.get_normal.argtypes = [C.c_void_p, C.c_uint32, C.POINTER(C.c_float)]
    LOAD_OBJ.get_tex2d.argtypes = [C.c_void_p, C.c_uint32, C.POINTER(C.c_float)]
    LOAD_OBJ.get_tangent.argtypes = [C.c_void_p, C.c_uint32, C.POINTER(C.c_float)]

    LOAD_OBJ.num_vertices.argtypes = [C.c_void_p]
    LOAD_OBJ.num_vertices.restype = C.c_ulong

    LOAD_OBJ.num_indices.argtypes = [C.c_void_p]
    LOAD_OBJ.num_indices.restype = C.c_ulong

    # unsigned long vertex_buffer_size(const void *m) 
    LOAD_OBJ.vertex_buffer_size.argtypes = [C.c_void_p]
    LOAD_OBJ.vertex_buffer_size.restype = C.c_ulong
    # unsigned long indices_offset(const void *m)
    LOAD_OBJ.indices_offset.argtypes = [C.c_void_p]
    LOAD_OBJ.indices_offset.restype = C.c_ulong
    # unsigned long index_buffer_size(const void *m) 
    LOAD_OBJ.index_buffer_size.argtypes = [C.c_void_p]
    LOAD_OBJ.index_buffer_size.restype = C.c_ulong
    # unsigned long total_buffer_size(const void *m) 
    LOAD_OBJ.total_buffer_size.argtypes = [C.c_void_p]
    LOAD_OBJ.total_buffer_size.restype = C.c_ulong
    # unsigned long packed_attr_size(const void *m)
    LOAD_OBJ.packed_attr_size.argtypes = [C.c_void_p]
    LOAD_OBJ.packed_attr_size.restype = C.c_ulong
    # EXPORT void init_memory_globals()
    # EXPORT void shutdown_memory_globals()

    LOAD_OBJ.init_memory_globals()
    MEMORY_GLOBALS_INIT = True

def close_learnogl():
    LOAD_OBJ.shutdown_memory_globals()
    MEMORY_GLOBALS_INIT = False

class LoadOptions:
    CALCULATE_TANGENTS = 0x1

class MeshData:
    def __init__(self):
        self.positions = None
        self.normals = None
        self.tex2ds = None
        self.indices = None


class Model:
    def __init__(self, model_pointer):
        num_meshes = LOAD_OBJ.num_meshes_in_model(model_pointer)

        self.meshes = [None] * num_meshes

        for i in range(num_meshes):
            self.meshes[i] = MeshData()
            md = self.meshes[i]

            md_pointer = LOAD_OBJ.get_mesh_data(model_pointer, C.c_uint32(i))

            mesh_data = LOAD_OBJ.get_mesh_data(model_pointer, i)
            stride = LOAD_OBJ.packed_attr_size(mesh_data)

            num_vertices = int(LOAD_OBJ.num_vertices(mesh_data))

            md.positions = np.zeros((num_vertices, 4), dtype='float32')
            md.normals = np.zeros((num_vertices, 3), dtype='float32')
            md.tex2ds = np.zeros((num_vertices, 2), dtype='float32')

            FLOAT4 = C.c_float * 4
            xyzw = FLOAT4()
            xyzw_p = C.cast(xyzw, C.POINTER(C.c_float))

            for i, (pos, normal, st) in enumerate(zip(md.positions, md.normals, md.tex2ds)):
                LOAD_OBJ.get_position(md_pointer, i, xyzw_p)
                pos[0] = xyzw[0]
                pos[1] = xyzw[1]
                pos[2] = xyzw[2]
                pos[3] = 1.0

                LOAD_OBJ.get_normal(md_pointer, i, xyzw_p)
                normal[0] = xyzw[0]
                normal[1] = xyzw[1]
                normal[2] = xyzw[2]

                LOAD_OBJ.get_tex2d(md_pointer, i, xyzw_p)
                st[0] = xyzw[0]
                st[1] = xyzw[1]

            md.indices = np.zeros((int(LOAD_OBJ.num_indices(mesh_data)),), dtype='int')
            index = C.cast(LOAD_OBJ.get_indices(mesh_data), C.POINTER(C.c_ushort))
            for i in range(len(md.indices)):
                md.indices[i] = int(index[i])

    @staticmethod
    def from_file(file, options):
        model_pointer = LOAD_OBJ.create_model()

        file_cstr = C.create_string_buffer(file.encode('utf-8'))
    
        result = LOAD_OBJ.load_model(model_pointer, file_cstr, C.c_uint32(options))
    
        if int(result) == 0:
            raise RuntimeError('Could not load model {}'.format(file))
    
        m = Model(model_pointer)

        LOAD_OBJ.delete_model(model_pointer)

        return m


# As a test, draw a wireframe of the loaded obj
if __name__ == '__main__':
    import sys
    import os

    if len(sys.argv) != 2:
        print('Usage: python {} <model_file>'.format(sys.argv[0]))
        sys.exit(1)

    init(os.path.join(os.getenv('HOME'), 'build/learnogl-debug/test/tiny_test/loadobj.so'))

    model = Model.from_file(sys.argv[1], LoadOptions.CALCULATE_TANGENTS)

    from pprint import pprint

    pprint(model.meshes[0].positions)
    pprint(model.meshes[0].normals)

    close_learnogl()
