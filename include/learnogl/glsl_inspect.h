// David Eberly, Geometric Tools, Redmond WA 98052
// Copyright (c) 1998-2017
// Distributed under the Boost Software License, Version 1.0.
// http://www.boost.org/LICENSE_1_0.txt
// http://www.geometrictools.com/License/Boost/LICENSE_1_0.txt
// File Version: 3.0.0 (2016/06/19)
// Edits by me, rksht

#pragma once

#include <learnogl/typed_gl_resources.h>

#include <learnogl/pmr_compatible_allocs.h>
#include <map>
#include <scaffold/string_stream.h>

// Query a program object for all information relevant to manipulating the
// program at run time.

namespace eng {

class InspectedGLSL {
  public:
    // Construction.  The input is the handle to a program that was
    // successfully created for the active context.
    InspectedGLSL(GLuint handle, pmr::memory_resource &pmr);

    // Named indices for the 'referencedBy' arrays.
    enum { ST_VERTEX, ST_GEOMETRY, ST_PIXEL, ST_COMPUTE, ST_TESSCONTROL, ST_TESSEVALUATION };

    struct Input {
        std::string name;
        GLint type;
        GLint location;
        GLint arraySize;
        GLint referencedBy[6];
        GLint isPerPatch;
        GLint locationComponent;
    };

    struct Output {
        std::string name;
        GLint type;
        GLint location;
        GLint arraySize;
        GLint referencedBy[6];
        GLint isPerPatch;
        GLint locationComponent;
        GLint locationIndex;
    };

    struct SamplerUniformInfo {
        // Helper info if you want to quickly know the kind of sampler
        bool isArray;
        bool isShadow;
        bool isCube;
        bool is1D, is2D, is3D;

        // In GL >= 4.2, the texture unit can be specified in GLSL source. This stores that.
        GLint textureUnit;
    };

    struct Uniform {
        std::string fullName;
        std::string name;
        GLint type;
        GLint location;
        GLint arraySize;
        GLint offset;
        GLint blockIndex;
        GLint arrayStride;
        GLint matrixStride;
        GLint isRowMajor;
        GLint atomicCounterBufferIndex;
        GLint referencedBy[6];
        ::optional<SamplerUniformInfo> optSamplerInfo;
    };

    struct DataBlock {
        std::string name;
        GLint bufferBinding;
        GLint bufferDataSize;
        GLint referencedBy[6];
        pmr::vector<GLint> activeVariables;
    };

    struct AtomicCounterBuffer {
        GLint bufferBinding;
        GLint bufferDataSize;
        GLint referencedBy[6];
        pmr::vector<GLint> activeVariables;
    };

    struct SubroutineUniform {
        std::string name;
        GLint location;
        GLint arraySize;
        pmr::vector<GLint> compatibleSubroutines;
    };

    struct BufferVariable {
        std::string fullName;
        std::string name;
        GLint type;
        GLint arraySize;
        GLint offset;
        GLint blockIndex;
        GLint arrayStride;
        GLint matrixStride;
        GLint isRowMajor;
        GLint topLevelArraySize;
        GLint topLevelArrayStride;
        GLint referencedBy[6];
    };

    struct TransformFeedbackVarying {
        std::string name;
        GLint type;
        GLint arraySize;
        GLint offset;
        GLint transformFeedbackBufferIndex;
    };

    struct TransformFeedbackBuffer {
        GLint bufferBinding;
        GLint transformFeedbackBufferStride;
        pmr::vector<GLint> activeVariables;
    };

    // Member access.
    inline GLuint GetProgramHandle() const;
    inline pmr::vector<Input> const &GetInputs() const;
    inline pmr::vector<Output> const &GetOutputs() const;
    inline pmr::vector<Uniform> const &GetUniforms() const;
    inline pmr::vector<DataBlock> const &GetUniformBlocks() const;
    inline pmr::vector<BufferVariable> const &GetBufferVariables() const;
    inline pmr::vector<DataBlock> const &GetBufferBlocks() const;
    inline pmr::vector<AtomicCounterBuffer> const &GetAtomicCounterBuffers() const;

    // This will not work on an instance based on a visual program.
    // This instance must correspond to a compute shader only program.
    void GetComputeShaderWorkGroupSize(GLint &numXThreads, GLint &numYThreads, GLint &numZThreads) const;

    // Print a normalfolk-readable representation to the string stream
    void Print(fo::string_stream::Buffer &ss) const;

  private:
    void ReflectProgramInputs();
    void ReflectProgramOutputs();
    void ReflectUniforms();
    void ReflectDataBlocks(GLenum programInterface, pmr::vector<DataBlock> &blocks);
    void ReflectAtomicCounterBuffers();
    void ReflectSubroutines(GLenum programInterface, pmr::vector<std::string> &subroutines);
    void ReflectSubroutineUniforms(GLenum programInterface, pmr::vector<SubroutineUniform> &subUniforms);
    void ReflectBufferVariables();
    void ReflectTransformFeedbackVaryings();
    void ReflectTransformFeedbackBuffers();

    GLuint mHandle;
    pmr::memory_resource *mPmrResource;

    DECL_PMR_VECTOR_MEMBER(Input, mInputs, *mPmrResource);
    DECL_PMR_VECTOR_MEMBER(Output, mOutputs, *mPmrResource);
    DECL_PMR_VECTOR_MEMBER(Uniform, mUniforms, *mPmrResource);
    DECL_PMR_VECTOR_MEMBER(DataBlock, mUniformBlocks, *mPmrResource);
    DECL_PMR_VECTOR_MEMBER(DataBlock, mShaderStorageBlocks, *mPmrResource);
    DECL_PMR_VECTOR_MEMBER(AtomicCounterBuffer, mAtomicCounterBuffers, *mPmrResource);
    DECL_PMR_VECTOR_MEMBER(std::string, mVertexSubroutines, *mPmrResource);
    DECL_PMR_VECTOR_MEMBER(std::string, mGeometrySubroutines, *mPmrResource);
    DECL_PMR_VECTOR_MEMBER(std::string, mPixelSubroutines, *mPmrResource);
    DECL_PMR_VECTOR_MEMBER(std::string, mComputeSubroutines, *mPmrResource);
    DECL_PMR_VECTOR_MEMBER(std::string, mTessControlSubroutines, *mPmrResource);
    DECL_PMR_VECTOR_MEMBER(std::string, mTessEvaluationSubroutines, *mPmrResource);
    DECL_PMR_VECTOR_MEMBER(SubroutineUniform, mVertexSubroutineUniforms, *mPmrResource);
    DECL_PMR_VECTOR_MEMBER(SubroutineUniform, mGeometrySubroutineUniforms, *mPmrResource);
    DECL_PMR_VECTOR_MEMBER(SubroutineUniform, mPixelSubroutineUniforms, *mPmrResource);
    DECL_PMR_VECTOR_MEMBER(SubroutineUniform, mComputeSubroutineUniforms, *mPmrResource);
    DECL_PMR_VECTOR_MEMBER(SubroutineUniform, mTessControlSubroutineUniforms, *mPmrResource);
    DECL_PMR_VECTOR_MEMBER(SubroutineUniform, mTessEvaluationSubroutineUniforms, *mPmrResource);
    DECL_PMR_VECTOR_MEMBER(BufferVariable, mBufferVariables, *mPmrResource);
    DECL_PMR_VECTOR_MEMBER(TransformFeedbackVarying, mTransformFeedbackVaryings, *mPmrResource);
    DECL_PMR_VECTOR_MEMBER(TransformFeedbackBuffer, mTransformFeedbackBuffers, *mPmrResource);

    // Used by Print() method to mape enums to strings.
    struct EnumMap {
        GLenum value;
        std::string name;
        std::string shaderName;
        unsigned rows; // use actual dim for straight vectors
        unsigned cols; // only use for cols in matrices
        unsigned size; // use 0 for opaques
    };
    static const EnumMap msEnumMap[];
    static unsigned
    GetEnumSize(GLenum value, GLint arraySize, GLint arrayStride, GLint matrixStride, GLint isRowMajor);
    static std::string GetEnumName(GLenum value);
    static std::string GetEnumShaderName(GLenum value);
    static std::string GetReferencedByShaderList(GLint const referencedBy[6]);

  private:
    // TODO: This is a workaround for an apparent bug in the Intel
    // HD 4600 OpenGL 4.3.0 (build 10.18.15.4281 and previous).
    // Sometimes a buffer object in a compute shader is reported as
    // unreferenced when in fact it is referenced.  Remove this once
    // the bug is fixed.
    void IntelWorkaround(std::string const &name, GLint results[]);
    bool mVendorIsIntel;
    std::map<GLenum, int> mShaderTypeMap;
};

inline GLuint InspectedGLSL::GetProgramHandle() const { return mHandle; }

inline pmr::vector<InspectedGLSL::Input> const &InspectedGLSL::GetInputs() const { return mInputs; }

inline pmr::vector<InspectedGLSL::Output> const &InspectedGLSL::GetOutputs() const { return mOutputs; }

inline pmr::vector<InspectedGLSL::Uniform> const &InspectedGLSL::GetUniforms() const { return mUniforms; }

inline pmr::vector<InspectedGLSL::DataBlock> const &InspectedGLSL::GetUniformBlocks() const {
    return mUniformBlocks;
}

inline pmr::vector<InspectedGLSL::BufferVariable> const &InspectedGLSL::GetBufferVariables() const {
    return mBufferVariables;
}

inline pmr::vector<InspectedGLSL::DataBlock> const &InspectedGLSL::GetBufferBlocks() const {
    return mShaderStorageBlocks;
}

inline pmr::vector<InspectedGLSL::AtomicCounterBuffer> const &InspectedGLSL::GetAtomicCounterBuffers() const {
    return mAtomicCounterBuffers;
}

} // namespace eng
