/*
 * Copyright (C) 2014 Patrick Mours
 * SPDX-License-Identifier: BSD-3-Clause OR MIT
 */

#include "opengl_impl_device.hpp"
#include "opengl_impl_device_context.hpp"
#include "opengl_impl_type_convert.hpp"
#include "opengl_hooks.hpp" // Fix name clashes with gl3w
#include "hook_manager.hpp"
#include <cstring> // std::memset, std::strlen

#define gl gl3wProcs.gl

struct DrawArraysIndirectCommand
{
	GLuint count;
	GLuint primcount;
	GLuint first;
	GLuint baseinstance;
};
struct DrawElementsIndirectCommand
{
	GLuint count;
	GLuint primcount;
	GLuint firstindex;
	GLuint basevertex;
	GLuint baseinstance;
};

// Initialize thread local variable in this translation unit, to avoid the compiler generating calls to '__dyn_tls_on_demand_init' on every use in the frequently called functions below
thread_local reshade::opengl::device_context_impl *g_opengl_context = nullptr;

#ifdef GL_VERSION_1_0
extern "C" void APIENTRY glTexImage1D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLint border, GLenum format, GLenum type, const GLvoid *pixels)
{
	static const auto trampoline = reshade::hooks::call(glTexImage1D);

	trampoline(target, level, internalformat, width, border, format, type, pixels);
}
extern "C" void APIENTRY glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels)
{
	static const auto trampoline = reshade::hooks::call(glTexImage2D);

	trampoline(target, level, internalformat, width, height, border, format, type, pixels);
}

extern "C" void APIENTRY glCopyPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum type)
{

	static const auto trampoline = reshade::hooks::call(glCopyPixels);
	trampoline(x, y, width, height, type);
}

extern "C" void APIENTRY glClear(GLbitfield mask)
{

	static const auto trampoline = reshade::hooks::call(glClear);
	trampoline(mask);
}

extern "C" void APIENTRY glEnable(GLenum cap)
{
	static const auto trampoline = reshade::hooks::call(glEnable);
	trampoline(cap);

}
extern "C" void APIENTRY glDisable(GLenum cap)
{
	static const auto trampoline = reshade::hooks::call(glDisable);
	trampoline(cap);

}

extern "C" void APIENTRY glCullFace(GLenum mode)
{
	static const auto trampoline = reshade::hooks::call(glCullFace);
	trampoline(mode);

}
extern "C" void APIENTRY glFrontFace(GLenum mode)
{
	static const auto trampoline = reshade::hooks::call(glFrontFace);
	trampoline(mode);

}
extern "C" void APIENTRY glHint(GLenum target, GLenum mode)
{
	static const auto trampoline = reshade::hooks::call(glHint);
	trampoline(target, mode);
}
extern "C" void APIENTRY glLineWidth(GLfloat width)
{
	static const auto trampoline = reshade::hooks::call(glLineWidth);
	trampoline(width);
}
extern "C" void APIENTRY glPointSize(GLfloat size)
{
	static const auto trampoline = reshade::hooks::call(glPointSize);
	trampoline(size);
}
extern "C" void APIENTRY glPolygonMode(GLenum face, GLenum mode)
{
	static const auto trampoline = reshade::hooks::call(glPolygonMode);
	trampoline(face, mode);

}

extern "C" void APIENTRY glAlphaFunc(GLenum func, GLclampf ref)
{
	static const auto trampoline = reshade::hooks::call(glAlphaFunc);
	trampoline(func, ref);

}
extern "C" void APIENTRY glBlendFunc(GLenum sfactor, GLenum dfactor)
{
	static const auto trampoline = reshade::hooks::call(glBlendFunc);
	trampoline(sfactor, dfactor);

}
extern "C" void APIENTRY glLogicOp(GLenum opcode)
{
	static const auto trampoline = reshade::hooks::call(glLogicOp);
	trampoline(opcode);

}
extern "C" void APIENTRY glColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha)
{
	static const auto trampoline = reshade::hooks::call(glColorMask);
	trampoline(red, green, blue, alpha);

}

extern "C" void APIENTRY glDepthFunc(GLenum func)
{
	static const auto trampoline = reshade::hooks::call(glDepthFunc);
	trampoline(func);

}
extern "C" void APIENTRY glDepthMask(GLboolean flag)
{
	static const auto trampoline = reshade::hooks::call(glDepthMask);
	trampoline(flag);

}

extern "C" void APIENTRY glStencilFunc(GLenum func, GLint ref, GLuint mask)
{
	static const auto trampoline = reshade::hooks::call(glStencilFunc);
	trampoline(func, ref, mask);

}
extern "C" void APIENTRY glStencilOp(GLenum fail, GLenum zfail, GLenum zpass)
{
	static const auto trampoline = reshade::hooks::call(glStencilOp);
	trampoline(fail, zfail, zpass);

}
extern "C" void APIENTRY glStencilMask(GLuint mask)
{
	static const auto trampoline = reshade::hooks::call(glStencilMask);
	trampoline(mask);

}

extern "C" void APIENTRY glScissor(GLint left, GLint bottom, GLsizei width, GLsizei height)
{
	static const auto trampoline = reshade::hooks::call(glScissor);
	trampoline(left, bottom, width, height);

}

extern "C" void APIENTRY glViewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
	static const auto trampoline = reshade::hooks::call(glViewport);
	trampoline(x, y, width, height);

}
extern "C" void APIENTRY glDepthRange(GLclampd zNear, GLclampd zFar)
{
	static const auto trampoline = reshade::hooks::call(glDepthRange);
	trampoline(zNear, zFar);
}
#endif

#ifdef GL_VERSION_1_1
extern "C" void APIENTRY glDeleteTextures(GLsizei n, const GLuint *textures)
{

	static const auto trampoline = reshade::hooks::call(glDeleteTextures);
	trampoline(n, textures);
}

extern "C" void APIENTRY glTexSubImage1D(GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format, GLenum type, const GLvoid *pixels)
{

	static const auto trampoline = reshade::hooks::call(glTexSubImage1D);
	trampoline(target, level, xoffset, width, format, type, pixels);
}
extern "C" void APIENTRY glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels)
{

	static const auto trampoline = reshade::hooks::call(glTexSubImage2D);
	trampoline(target, level, xoffset, yoffset, width, height, format, type, pixels);
}

extern "C" void APIENTRY glCopyTexImage1D(GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLint border)
{
	static const auto trampoline = reshade::hooks::call(glCopyTexImage1D);
	trampoline(target, level, internalformat, x, y, width, border);
}
extern "C" void APIENTRY glCopyTexImage2D(GLenum target, GLint level, GLenum internalFormat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border)
{
	static const auto trampoline = reshade::hooks::call(glCopyTexImage2D);
	trampoline(target, level, internalFormat, x, y, width, height, border);
}

extern "C" void APIENTRY glCopyTexSubImage1D(GLenum target, GLint level, GLint xoffset, GLint x, GLint y, GLsizei width)
{

	static const auto trampoline = reshade::hooks::call(glCopyTexSubImage1D);
	trampoline(target, level, xoffset, x, y, width);
}
extern "C" void APIENTRY glCopyTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height)
{

	static const auto trampoline = reshade::hooks::call(glCopyTexSubImage2D);
	trampoline(target, level, xoffset, yoffset, x, y, width, height);
}

extern "C" void APIENTRY glBindTexture(GLenum target, GLuint texture)
{
	static const auto trampoline = reshade::hooks::call(glBindTexture);
	trampoline(target, texture);

}

extern "C" void APIENTRY glPolygonOffset(GLfloat factor, GLfloat units)
{
	static const auto trampoline = reshade::hooks::call(glPolygonOffset);
	trampoline(factor, units);

}

extern "C" void APIENTRY glDrawArrays(GLenum mode, GLint first, GLsizei count)
{

	static const auto trampoline = reshade::hooks::call(glDrawArrays);
	trampoline(mode, first, count);
}
extern "C" void APIENTRY glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices)
{

	static const auto trampoline = reshade::hooks::call(glDrawElements);
	trampoline(mode, count, type, indices);
}
#endif

#ifdef GL_VERSION_1_2
void APIENTRY glTexImage3D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLenum format, GLenum type, const GLvoid *pixels)
{
	static const auto trampoline = reshade::hooks::call(glTexImage3D);

	trampoline(target, level, internalformat, width, height, depth, border, format, type, pixels);
}

void APIENTRY glTexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, const void *pixels)
{

	static const auto trampoline = reshade::hooks::call(glTexSubImage3D);
	trampoline(target, level, xoffset, yoffset, zoffset, width, height, depth, format, type, pixels);
}

void APIENTRY glCopyTexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLint x, GLint y, GLsizei width, GLsizei height)
{

	static const auto trampoline = reshade::hooks::call(glCopyTexSubImage3D);
	trampoline(target, level, xoffset, yoffset, zoffset, x, y, width, height);
}

void APIENTRY glDrawRangeElements(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const GLvoid *indices)
{

	static const auto trampoline = reshade::hooks::call(glDrawRangeElements);
	trampoline(mode, start, end, count, type, indices);
}
#endif

#ifdef GL_VERSION_1_3
void APIENTRY glCompressedTexImage1D(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLint border, GLsizei imageSize, const void *data)
{
	static const auto trampoline = reshade::hooks::call(glCompressedTexImage1D);

	trampoline(target, level, internalformat, width, border, imageSize, data);
}
void APIENTRY glCompressedTexImage2D(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const void *data)
{
	static const auto trampoline = reshade::hooks::call(glCompressedTexImage2D);
	trampoline(target, level, internalformat, width, height, border, imageSize, data);
}
void APIENTRY glCompressedTexImage3D(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLsizei imageSize, const void *data)
{
	static const auto trampoline = reshade::hooks::call(glCompressedTexImage3D);
	trampoline(target, level, internalformat, width, height, depth, border, imageSize, data);
}

void APIENTRY glCompressedTexSubImage1D(GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format, GLsizei imageSize, const void *data)
{

	static const auto trampoline = reshade::hooks::call(glCompressedTexSubImage1D);
	trampoline(target, level, xoffset, width, format, imageSize, data);
}
void APIENTRY glCompressedTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLsizei imageSize, const void *data)
{

	static const auto trampoline = reshade::hooks::call(glCompressedTexSubImage2D);
	trampoline(target, level, xoffset, yoffset, width, height, format, imageSize, data);
}
void APIENTRY glCompressedTexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLsizei imageSize, const void *data)
{

	static const auto trampoline = reshade::hooks::call(glCompressedTexSubImage3D);
	trampoline(target, level, xoffset, yoffset, zoffset, width, height, depth, format, imageSize, data);
}
#endif

#ifdef GL_VERSION_1_4
void APIENTRY glBlendFuncSeparate(GLenum sfactorRGB, GLenum dfactorRGB, GLenum sfactorAlpha, GLenum dfactorAlpha)
{
	static const auto trampoline = reshade::hooks::call(glBlendFuncSeparate);
	trampoline(sfactorRGB, dfactorRGB, sfactorAlpha, dfactorAlpha);

}
void APIENTRY glBlendColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
	static const auto trampoline = reshade::hooks::call(glBlendColor);
	trampoline(red, green, blue, alpha);

}
void APIENTRY glBlendEquation(GLenum mode)
{
	static const auto trampoline = reshade::hooks::call(glBlendEquation);
	trampoline(mode);

}

void APIENTRY glMultiDrawArrays(GLenum mode, const GLint *first, const GLsizei *count, GLsizei drawcount)
{

	static const auto trampoline = reshade::hooks::call(glMultiDrawArrays);
	trampoline(mode, first, count, drawcount);
}
void APIENTRY glMultiDrawElements(GLenum mode, const GLsizei *count, GLenum type, const GLvoid *const *indices, GLsizei drawcount)
{

	static const auto trampoline = reshade::hooks::call(glMultiDrawElements);
	trampoline(mode, count, type, indices, drawcount);
}
#endif

#ifdef GL_VERSION_1_5
void APIENTRY glDeleteBuffers(GLsizei n, const GLuint *buffers)
{

	static const auto trampoline = reshade::hooks::call(glDeleteBuffers);
	trampoline(n, buffers);
}

void APIENTRY glBufferData(GLenum target, GLsizeiptr size, const void *data, GLenum usage)
{
	static const auto trampoline = reshade::hooks::call(glBufferData);

	trampoline(target, size, data, usage);
}

void APIENTRY glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void *data)
{

	static const auto trampoline = reshade::hooks::call(glBufferSubData);
	trampoline(target, offset, size, data);
}

auto APIENTRY glMapBuffer(GLenum target, GLenum access) -> void *
{
	static const auto trampoline = reshade::hooks::call(glMapBuffer);
	void *result = trampoline(target, access);


	return result;
}
void APIENTRY glUnmapBuffer(GLenum target)
{

	static const auto trampoline = reshade::hooks::call(glUnmapBuffer);
	trampoline(target);
}

void APIENTRY glBindBuffer(GLenum target, GLuint buffer)
{
	static const auto trampoline = reshade::hooks::call(glBindBuffer);
	trampoline(target, buffer);

}
#endif

#ifdef GL_VERSION_2_0
void APIENTRY glDeleteProgram(GLuint program)
{

	static const auto trampoline = reshade::hooks::call(glDeleteProgram);
	trampoline(program);
}

void APIENTRY glLinkProgram(GLuint program)
{
	static const auto trampoline = reshade::hooks::call(glLinkProgram);
	trampoline(program);

}

void APIENTRY glShaderSource(GLuint shader, GLsizei count, const GLchar *const *string, const GLint *length)
{

	static const auto trampoline = reshade::hooks::call(glShaderSource);
	trampoline(shader, count, string, length);
}

void APIENTRY glUseProgram(GLuint program)
{
	static const auto trampoline = reshade::hooks::call(glUseProgram);
	trampoline(program);

}

void APIENTRY glBlendEquationSeparate(GLenum modeRGB, GLenum modeAlpha)
{
	static const auto trampoline = reshade::hooks::call(glBlendEquationSeparate);
	trampoline(modeRGB, modeAlpha);

}

void APIENTRY glStencilFuncSeparate(GLenum face, GLenum func, GLint ref, GLuint mask)
{
	static const auto trampoline = reshade::hooks::call(glStencilFuncSeparate);
	trampoline(face, func, ref, mask);

}
void APIENTRY glStencilOpSeparate(GLenum face, GLenum fail, GLenum zfail, GLenum zpass)
{
	static const auto trampoline = reshade::hooks::call(glStencilOpSeparate);
	trampoline(face, fail, zfail, zpass);

}
void APIENTRY glStencilMaskSeparate(GLenum face, GLuint mask)
{
	static const auto trampoline = reshade::hooks::call(glStencilMaskSeparate);
	trampoline(face, mask);

}

void APIENTRY glUniform1f(GLint location, GLfloat v0)
{
	static const auto trampoline = reshade::hooks::call(glUniform1f);
	trampoline(location, v0);

}
void APIENTRY glUniform2f(GLint location, GLfloat v0, GLfloat v1)
{
	static const auto trampoline = reshade::hooks::call(glUniform2f);
	trampoline(location, v0, v1);

}
void APIENTRY glUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2)
{
	static const auto trampoline = reshade::hooks::call(glUniform3f);
	trampoline(location, v0, v1, v2);

}
void APIENTRY glUniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3)
{
	static const auto trampoline = reshade::hooks::call(glUniform4f);
	trampoline(location, v0, v1, v2, v3);

}
void APIENTRY glUniform1i(GLint location, GLint v0)
{
	static const auto trampoline = reshade::hooks::call(glUniform1i);
	trampoline(location, v0);

}
void APIENTRY glUniform2i(GLint location, GLint v0, GLint v1)
{
	static const auto trampoline = reshade::hooks::call(glUniform2i);
	trampoline(location, v0, v1);

}
void APIENTRY glUniform3i(GLint location, GLint v0, GLint v1, GLint v2)
{
	static const auto trampoline = reshade::hooks::call(glUniform3i);
	trampoline(location, v0, v1, v2);

}
void APIENTRY glUniform4i(GLint location, GLint v0, GLint v1, GLint v2, GLint v3)
{
	static const auto trampoline = reshade::hooks::call(glUniform4i);
	trampoline(location, v0, v1, v2, v3);

}

void APIENTRY glUniform1fv(GLint location, GLsizei count, const GLfloat *value)
{
	static const auto trampoline = reshade::hooks::call(glUniform1fv);
	trampoline(location, count, value);

}
void APIENTRY glUniform2fv(GLint location, GLsizei count, const GLfloat *value)
{
	static const auto trampoline = reshade::hooks::call(glUniform2fv);
	trampoline(location, count, value);

}
void APIENTRY glUniform3fv(GLint location, GLsizei count, const GLfloat *value)
{
	static const auto trampoline = reshade::hooks::call(glUniform3fv);
	trampoline(location, count, value);

}
void APIENTRY glUniform4fv(GLint location, GLsizei count, const GLfloat *value)
{
	static const auto trampoline = reshade::hooks::call(glUniform4fv);
	trampoline(location, count, value);

}
void APIENTRY glUniform1iv(GLint location, GLsizei count, const GLint *value)
{
	static const auto trampoline = reshade::hooks::call(glUniform1iv);
	trampoline(location, count, value);

}
void APIENTRY glUniform2iv(GLint location, GLsizei count, const GLint *value)
{
	static const auto trampoline = reshade::hooks::call(glUniform2iv);
	trampoline(location, count, value);

}
void APIENTRY glUniform3iv(GLint location, GLsizei count, const GLint *value)
{
	static const auto trampoline = reshade::hooks::call(glUniform3iv);
	trampoline(location, count, value);

}
void APIENTRY glUniform4iv(GLint location, GLsizei count, const GLint *value)
{
	static const auto trampoline = reshade::hooks::call(glUniform4iv);
	trampoline(location, count, value);

}
void APIENTRY glUniformMatrix2fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
	static const auto trampoline = reshade::hooks::call(glUniformMatrix2fv);
	trampoline(location, count, transpose, value);

}
void APIENTRY glUniformMatrix3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
	static const auto trampoline = reshade::hooks::call(glUniformMatrix3fv);
	trampoline(location, count, transpose, value);

}
void APIENTRY glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
	static const auto trampoline = reshade::hooks::call(glUniformMatrix4fv);
	trampoline(location, count, transpose, value);

}

void APIENTRY glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer)
{
	static const auto trampoline = reshade::hooks::call(glVertexAttribPointer);
	trampoline(index, size, type, normalized, stride, pointer);

}
#endif

#ifdef GL_VERSION_2_1
void APIENTRY glUniformMatrix2x3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
	static const auto trampoline = reshade::hooks::call(glUniformMatrix2x3fv);
	trampoline(location, count, transpose, value);

}
void APIENTRY glUniformMatrix3x2fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
	static const auto trampoline = reshade::hooks::call(glUniformMatrix3x2fv);
	trampoline(location, count, transpose, value);

}
void APIENTRY glUniformMatrix2x4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
	static const auto trampoline = reshade::hooks::call(glUniformMatrix2x4fv);
	trampoline(location, count, transpose, value);

}
void APIENTRY glUniformMatrix4x2fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
	static const auto trampoline = reshade::hooks::call(glUniformMatrix4x2fv);
	trampoline(location, count, transpose, value);

}
void APIENTRY glUniformMatrix3x4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
	static const auto trampoline = reshade::hooks::call(glUniformMatrix3x4fv);
	trampoline(location, count, transpose, value);

}
void APIENTRY glUniformMatrix4x3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
	static const auto trampoline = reshade::hooks::call(glUniformMatrix3x4fv);
	trampoline(location, count, transpose, value);

}
#endif

#ifdef GL_VERSION_3_0
void APIENTRY glDeleteRenderbuffers(GLsizei n, const GLuint *renderbuffers)
{

	static const auto trampoline = reshade::hooks::call(glDeleteRenderbuffers);
	trampoline(n, renderbuffers);
}

void APIENTRY glDeleteVertexArrays(GLsizei n, const GLuint *arrays)
{

	static const auto trampoline = reshade::hooks::call(glDeleteVertexArrays);
	trampoline(n, arrays);
}

void APIENTRY glFramebufferTexture1D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level)
{
	static const auto trampoline = reshade::hooks::call(glFramebufferTexture1D);
	trampoline(target, attachment, textarget, texture, level);

}
void APIENTRY glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level)
{
	static const auto trampoline = reshade::hooks::call(glFramebufferTexture2D);
	trampoline(target, attachment, textarget, texture, level);

}
void APIENTRY glFramebufferTexture3D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level, GLint layer)
{
	static const auto trampoline = reshade::hooks::call(glFramebufferTexture3D);
	trampoline(target, attachment, textarget, texture, level, layer);

}
void APIENTRY glFramebufferTextureLayer(GLenum target, GLenum attachment, GLuint texture, GLint level, GLint layer)
{
	static const auto trampoline = reshade::hooks::call(glFramebufferTextureLayer);
	trampoline(target, attachment, texture, level, layer);

}
void APIENTRY glFramebufferRenderbuffer(GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer)
{
	static const auto trampoline = reshade::hooks::call(glFramebufferRenderbuffer);
	trampoline(target, attachment, renderbuffertarget, renderbuffer);

}

void APIENTRY glRenderbufferStorage(GLenum target, GLenum internalformat, GLsizei width, GLsizei height)
{
	static const auto trampoline = reshade::hooks::call(glRenderbufferStorage);

	trampoline(target, internalformat, width, height);
}
void APIENTRY glRenderbufferStorageMultisample(GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height)
{
	static const auto trampoline = reshade::hooks::call(glRenderbufferStorageMultisample);

	trampoline(target, samples, internalformat, width, height);
}

auto APIENTRY glMapBufferRange(GLenum target, GLintptr offset, GLsizeiptr length, GLenum access) -> void *
{
	static const auto trampoline = reshade::hooks::call(glMapBufferRange);
	void *result = trampoline(target, offset, length, access);


	return result;
}

void APIENTRY glClearBufferiv(GLenum buffer, GLint drawbuffer, const GLint *value)
{

	static const auto trampoline = reshade::hooks::call(glClearBufferiv);
	trampoline(buffer, drawbuffer, value);
}
void APIENTRY glClearBufferuiv(GLenum buffer, GLint drawbuffer, const GLuint *value)
{

	static const auto trampoline = reshade::hooks::call(glClearBufferuiv);
	trampoline(buffer, drawbuffer, value);
}
void APIENTRY glClearBufferfv(GLenum buffer, GLint drawbuffer, const GLfloat *value)
{

	static const auto trampoline = reshade::hooks::call(glClearBufferfv);
	trampoline(buffer, drawbuffer, value);
}
void APIENTRY glClearBufferfi(GLenum buffer, GLint drawbuffer, GLfloat depth, GLint stencil)
{

	static const auto trampoline = reshade::hooks::call(glClearBufferfi);
	trampoline(buffer, drawbuffer, depth, stencil);
}

void APIENTRY glBlitFramebuffer(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter)
{

	static const auto trampoline = reshade::hooks::call(glBlitFramebuffer);
	trampoline(srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1, mask, filter);
}

void APIENTRY glGenerateMipmap(GLenum target)
{

	static const auto trampoline = reshade::hooks::call(glGenerateMipmap);
	trampoline(target);
}

void APIENTRY glBindBufferBase(GLenum target, GLuint index, GLuint buffer)
{
	static const auto trampoline = reshade::hooks::call(glBindBufferBase);
	trampoline(target, index, buffer);

}
void APIENTRY glBindBufferRange(GLenum target, GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size)
{
	static const auto trampoline = reshade::hooks::call(glBindBufferRange);
	trampoline(target, index, buffer, offset, size);

}

void APIENTRY glBindFramebuffer(GLenum target, GLuint framebuffer)
{
	static const auto trampoline = reshade::hooks::call(glBindFramebuffer);
	trampoline(target, framebuffer);

}

void APIENTRY glBindVertexArray(GLuint array)
{
	static const auto trampoline = reshade::hooks::call(glBindVertexArray);
	trampoline(array);

}

void APIENTRY glUniform1ui(GLint location, GLuint v0)
{
	static const auto trampoline = reshade::hooks::call(glUniform1ui);
	trampoline(location, v0);

}
void APIENTRY glUniform2ui(GLint location, GLuint v0, GLuint v1)
{
	static const auto trampoline = reshade::hooks::call(glUniform2ui);
	trampoline(location, v0, v1);

}
void APIENTRY glUniform3ui(GLint location, GLuint v0, GLuint v1, GLuint v2)
{
	static const auto trampoline = reshade::hooks::call(glUniform3ui);
	trampoline(location, v0, v1, v2);

}
void APIENTRY glUniform4ui(GLint location, GLuint v0, GLuint v1, GLuint v2, GLuint v3)
{
	static const auto trampoline = reshade::hooks::call(glUniform4ui);
	trampoline(location, v0, v1, v2, v3);

}

void APIENTRY glUniform1uiv(GLint location, GLsizei count, const GLuint *value)
{
	static const auto trampoline = reshade::hooks::call(glUniform1uiv);
	trampoline(location, count, value);

}
void APIENTRY glUniform2uiv(GLint location, GLsizei count, const GLuint *value)
{
	static const auto trampoline = reshade::hooks::call(glUniform2uiv);
	trampoline(location, count, value);

}
void APIENTRY glUniform3uiv(GLint location, GLsizei count, const GLuint *value)
{
	static const auto trampoline = reshade::hooks::call(glUniform3uiv);
	trampoline(location, count, value);

}
void APIENTRY glUniform4uiv(GLint location, GLsizei count, const GLuint *value)
{
	static const auto trampoline = reshade::hooks::call(glUniform4uiv);
	trampoline(location, count, value);

}

void APIENTRY glVertexAttribIPointer(GLuint index, GLint size, GLenum type, GLsizei stride, const void *pointer)
{
	static const auto trampoline = reshade::hooks::call(glVertexAttribIPointer);
	trampoline(index, size, type, stride, pointer);

}
#endif

#ifdef GL_VERSION_3_1
void APIENTRY glTexBuffer(GLenum target, GLenum internalformat, GLuint buffer)
{
	static const auto trampoline = reshade::hooks::call(glTexBuffer);
	trampoline(target, internalformat, buffer);
}

void APIENTRY glCopyBufferSubData(GLenum readTarget, GLenum writeTarget, GLintptr readOffset, GLintptr writeOffset, GLsizeiptr size)
{

	static const auto trampoline = reshade::hooks::call(glCopyBufferSubData);
	trampoline(readTarget, writeTarget, readOffset, writeOffset, size);
}

void APIENTRY glDrawArraysInstanced(GLenum mode, GLint first, GLsizei count, GLsizei primcount)
{

	static const auto trampoline = reshade::hooks::call(glDrawArraysInstanced);
	trampoline(mode, first, count, primcount);
}
void APIENTRY glDrawElementsInstanced(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices, GLsizei primcount)
{

	static const auto trampoline = reshade::hooks::call(glDrawElementsInstanced);
	trampoline(mode, count, type, indices, primcount);
}
#endif

#ifdef GL_VERSION_3_2
void APIENTRY glFramebufferTexture(GLenum target, GLenum attachment, GLuint texture, GLint level)
{
	static const auto trampoline = reshade::hooks::call(glFramebufferTexture);
	trampoline(target, attachment, texture, level);

}

void APIENTRY glTexImage2DMultisample(GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height, GLboolean fixedsamplelocations)
{
	static const auto trampoline = reshade::hooks::call(glTexImage2DMultisample);

	trampoline(target, samples, internalformat, width, height, fixedsamplelocations);
}
void APIENTRY glTexImage3DMultisample(GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth, GLboolean fixedsamplelocations)
{
	static const auto trampoline = reshade::hooks::call(glTexImage3DMultisample);

	trampoline(target, samples, internalformat, width, height, depth, fixedsamplelocations);
}

void APIENTRY glDrawElementsBaseVertex(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices, GLint basevertex)
{

	static const auto trampoline = reshade::hooks::call(glDrawElementsBaseVertex);
	trampoline(mode, count, type, indices, basevertex);
}
void APIENTRY glDrawRangeElementsBaseVertex(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const GLvoid *indices, GLint basevertex)
{

	static const auto trampoline = reshade::hooks::call(glDrawRangeElementsBaseVertex);
	trampoline(mode, start, end, count, type, indices, basevertex);
}
void APIENTRY glDrawElementsInstancedBaseVertex(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices, GLsizei primcount, GLint basevertex)
{

	static const auto trampoline = reshade::hooks::call(glDrawElementsInstancedBaseVertex);
	trampoline(mode, count, type, indices, primcount, basevertex);
}
void APIENTRY glMultiDrawElementsBaseVertex(GLenum mode, const GLsizei *count, GLenum type, const GLvoid *const *indices, GLsizei drawcount, const GLint *basevertex)
{

	static const auto trampoline = reshade::hooks::call(glMultiDrawElementsBaseVertex);
	trampoline(mode, count, type, indices, drawcount, basevertex);
}
#endif

#ifdef GL_VERSION_4_0
void APIENTRY glDrawArraysIndirect(GLenum mode, const GLvoid *indirect)
{

	static const auto trampoline = reshade::hooks::call(glDrawArraysIndirect);
	trampoline(mode, indirect);
}
void APIENTRY glDrawElementsIndirect(GLenum mode, GLenum type, const GLvoid *indirect)
{

	static const auto trampoline = reshade::hooks::call(glDrawElementsIndirect);
	trampoline(mode, type, indirect);
}
#endif

#ifdef GL_VERSION_4_1
void APIENTRY glScissorArrayv(GLuint first, GLsizei count, const GLint *v)
{
	static const auto trampoline = reshade::hooks::call(glScissorArrayv);
	trampoline(first, count, v);

}
void APIENTRY glScissorIndexed(GLuint index, GLint left, GLint bottom, GLsizei width, GLsizei height)
{
	static const auto trampoline = reshade::hooks::call(glScissorIndexed);
	trampoline(index, left, bottom, width, height);

}
void APIENTRY glScissorIndexedv(GLuint index, const GLint *v)
{
	static const auto trampoline = reshade::hooks::call(glScissorIndexedv);
	trampoline(index, v);

}

void APIENTRY glViewportArrayv(GLuint first, GLsizei count, const GLfloat *v)
{
	static const auto trampoline = reshade::hooks::call(glViewportArrayv);
	trampoline(first, count, v);

}
void APIENTRY glViewportIndexedf(GLuint index, GLfloat x, GLfloat y, GLfloat w, GLfloat h)
{
	static const auto trampoline = reshade::hooks::call(glViewportIndexedf);
	trampoline(index, x, y, w, h);

}
void APIENTRY glViewportIndexedfv(GLuint index, const GLfloat *v)
{
	static const auto trampoline = reshade::hooks::call(glViewportIndexedfv);
	trampoline(index, v);

}

void APIENTRY glVertexAttribLPointer(GLuint index, GLint size, GLenum type, GLsizei stride, const void *pointer)
{
	static const auto trampoline = reshade::hooks::call(glVertexAttribLPointer);
	trampoline(index, size, type, stride, pointer);

}
#endif

#ifdef GL_VERSION_4_2
void APIENTRY glTexStorage1D(GLenum target, GLsizei levels, GLenum internalformat, GLsizei width)
{
	static const auto trampoline = reshade::hooks::call(glTexStorage1D);

	trampoline(target, levels, internalformat, width);
}
void APIENTRY glTexStorage2D(GLenum target, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height)
{
	static const auto trampoline = reshade::hooks::call(glTexStorage2D);

	trampoline(target, levels, internalformat, width, height);
}
void APIENTRY glTexStorage3D(GLenum target, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth)
{
	static const auto trampoline = reshade::hooks::call(glTexStorage3D);

	trampoline(target, levels, internalformat, width, height, depth);
}

void APIENTRY glBindImageTexture(GLuint unit, GLuint texture, GLint level, GLboolean layered, GLint layer, GLenum access, GLenum format)
{
	static const auto trampoline = reshade::hooks::call(glBindImageTexture);
	trampoline(unit, texture, level, layered, layer, access, format);

}

void APIENTRY glDrawArraysInstancedBaseInstance(GLenum mode, GLint first, GLsizei count, GLsizei primcount, GLuint baseinstance)
{

	static const auto trampoline = reshade::hooks::call(glDrawArraysInstancedBaseInstance);
	trampoline(mode, first, count, primcount, baseinstance);
}
void APIENTRY glDrawElementsInstancedBaseInstance(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices, GLsizei primcount, GLuint baseinstance)
{

	static const auto trampoline = reshade::hooks::call(glDrawElementsInstancedBaseInstance);
	trampoline(mode, count, type, indices, primcount, baseinstance);
}
void APIENTRY glDrawElementsInstancedBaseVertexBaseInstance(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices, GLsizei primcount, GLint basevertex, GLuint baseinstance)
{

	static const auto trampoline = reshade::hooks::call(glDrawElementsInstancedBaseVertexBaseInstance);
	trampoline(mode, count, type, indices, primcount, basevertex, baseinstance);
}
#endif

#ifdef GL_VERSION_4_3
void APIENTRY glTextureView(GLuint texture, GLenum target, GLuint origtexture, GLenum internalformat, GLuint minlevel, GLuint numlevels, GLuint minlayer, GLuint numlayers)
{
	static const auto trampoline = reshade::hooks::call(glTextureView);

	trampoline(texture, target, origtexture, internalformat, minlevel, numlevels, minlayer, numlayers);
}

void APIENTRY glTexBufferRange(GLenum target, GLenum internalformat, GLuint buffer, GLintptr offset, GLsizeiptr size)
{
	static const auto trampoline = reshade::hooks::call(glTexBufferRange);
	trampoline(target, internalformat, buffer, offset, size);
}

void APIENTRY glTexStorage2DMultisample(GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height, GLboolean fixedsamplelocations)
{
	static const auto trampoline = reshade::hooks::call(glTexStorage2DMultisample);

	trampoline(target, samples, internalformat, width, height, fixedsamplelocations);
}
void APIENTRY glTexStorage3DMultisample(GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth, GLboolean fixedsamplelocations)
{
	static const auto trampoline = reshade::hooks::call(glTexStorage3DMultisample);

	trampoline(target, samples, internalformat, width, height, depth, fixedsamplelocations);
}

void APIENTRY glCopyImageSubData(GLuint srcName, GLenum srcTarget, GLint srcLevel, GLint srcX, GLint srcY, GLint srcZ, GLuint dstName, GLenum dstTarget, GLint dstLevel, GLint dstX, GLint dstY, GLint dstZ, GLsizei srcWidth, GLsizei srcHeight, GLsizei srcDepth)
{

	static const auto trampoline = reshade::hooks::call(glCopyImageSubData);
	trampoline(srcName, srcTarget, srcLevel, srcX, srcY, srcZ, dstName, dstTarget, dstLevel, dstX, dstY, dstZ, srcWidth, srcHeight, srcDepth);
}

void APIENTRY glBindVertexBuffer(GLuint bindingindex, GLuint buffer, GLintptr offset, GLsizei stride)
{
	static const auto trampoline = reshade::hooks::call(glBindVertexBuffer);
	trampoline(bindingindex, buffer, offset, stride);

}

void APIENTRY glDispatchCompute(GLuint num_groups_x, GLuint num_groups_y, GLuint num_groups_z)
{

	static const auto trampoline = reshade::hooks::call(glDispatchCompute);
	trampoline(num_groups_x, num_groups_y, num_groups_z);
}
void APIENTRY glDispatchComputeIndirect(GLintptr indirect)
{

	static const auto trampoline = reshade::hooks::call(glDispatchComputeIndirect);
	trampoline(indirect);
}

void APIENTRY glMultiDrawArraysIndirect(GLenum mode, const void *indirect, GLsizei drawcount, GLsizei stride)
{

	static const auto trampoline = reshade::hooks::call(glMultiDrawArraysIndirect);
	trampoline(mode, indirect, drawcount, stride);
}
void APIENTRY glMultiDrawElementsIndirect(GLenum mode, GLenum type, const void *indirect, GLsizei drawcount, GLsizei stride)
{

	static const auto trampoline = reshade::hooks::call(glMultiDrawElementsIndirect);
	trampoline(mode, type, indirect, drawcount, stride);
}
#endif

#ifdef GL_VERSION_4_4
void APIENTRY glBufferStorage(GLenum target, GLsizeiptr size, const void *data, GLbitfield flags)
{
	static const auto trampoline = reshade::hooks::call(glBufferStorage);

	trampoline(target, size, data, flags);
}

void APIENTRY glBindBuffersBase(GLenum target, GLuint first, GLsizei count, const GLuint *buffers)
{
	static const auto trampoline = reshade::hooks::call(glBindBuffersBase);
	trampoline(target, first, count, buffers);

}
void APIENTRY glBindBuffersRange(GLenum target, GLuint first, GLsizei count, const GLuint *buffers, const GLintptr *offsets, const GLintptr *sizes)
{
	static const auto trampoline = reshade::hooks::call(glBindBuffersRange);
	trampoline(target, first, count, buffers, offsets, sizes);

}

void APIENTRY glBindTextures(GLuint first, GLsizei count, const GLuint *textures)
{
	static const auto trampoline = reshade::hooks::call(glBindTextures);
	trampoline(first, count, textures);

}

void APIENTRY glBindImageTextures(GLuint first, GLsizei count, const GLuint *textures)
{
	static const auto trampoline = reshade::hooks::call(glBindImageTextures);
	trampoline(first, count, textures);

}

void APIENTRY glBindVertexBuffers(GLuint first, GLsizei count, const GLuint *buffers, const GLintptr *offsets, const GLsizei *strides)
{
	static const auto trampoline = reshade::hooks::call(glBindVertexBuffers);
	trampoline(first, count, buffers, offsets, strides);

}
#endif

#ifdef GL_VERSION_4_5
void APIENTRY glTextureBuffer(GLuint texture, GLenum internalformat, GLuint buffer)
{
	static const auto trampoline = reshade::hooks::call(glTextureBuffer);
	trampoline(texture, internalformat, buffer);
}
void APIENTRY glTextureBufferRange(GLuint texture, GLenum internalformat, GLuint buffer, GLintptr offset, GLsizeiptr size)
{
	static const auto trampoline = reshade::hooks::call(glTextureBufferRange);
	trampoline(texture, internalformat, buffer, offset, size);
}

void APIENTRY glNamedBufferData(GLuint buffer, GLsizeiptr size, const void *data, GLenum usage)
{
	static const auto trampoline = reshade::hooks::call(glNamedBufferData);

	trampoline(buffer, size, data, usage);
}

void APIENTRY glNamedBufferStorage(GLuint buffer, GLsizeiptr size, const void *data, GLbitfield flags)
{
	static const auto trampoline = reshade::hooks::call(glNamedBufferStorage);

	trampoline(buffer, size, data, flags);
}

void APIENTRY glTextureStorage1D(GLuint texture, GLsizei levels, GLenum internalformat, GLsizei width)
{
	static const auto trampoline = reshade::hooks::call(glTextureStorage1D);

	trampoline(texture, levels, internalformat, width);
}
void APIENTRY glTextureStorage2D(GLuint texture, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height)
{
	static const auto trampoline = reshade::hooks::call(glTextureStorage2D);

	trampoline(texture, levels, internalformat, width, height);
}
void APIENTRY glTextureStorage2DMultisample(GLuint texture, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height, GLboolean fixedsamplelocations)
{
	static const auto trampoline = reshade::hooks::call(glTextureStorage2DMultisample);

	trampoline(texture, samples, internalformat, width, height, fixedsamplelocations);
}
void APIENTRY glTextureStorage3D(GLuint texture, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth)
{
	static const auto trampoline = reshade::hooks::call(glTextureStorage3D);

	trampoline(texture, levels, internalformat, width, height, depth);
}
void APIENTRY glTextureStorage3DMultisample(GLuint texture, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth, GLboolean fixedsamplelocations)
{
	static const auto trampoline = reshade::hooks::call(glTextureStorage3DMultisample);

	trampoline(texture, samples, internalformat, width, height, depth, fixedsamplelocations);
}

void APIENTRY glNamedBufferSubData(GLuint buffer, GLintptr offset, GLsizeiptr size, const void *data)
{

	static const auto trampoline = reshade::hooks::call(glNamedBufferSubData);
	trampoline(buffer, offset, size, data);
}

void APIENTRY glTextureSubImage1D(GLuint texture, GLint level, GLint xoffset, GLsizei width, GLenum format, GLenum type, const GLvoid *pixels)
{

	static const auto trampoline = reshade::hooks::call(glTextureSubImage1D);
	trampoline(texture, level, xoffset, width, format, type, pixels);
}
void APIENTRY glTextureSubImage2D(GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels)
{

	static const auto trampoline = reshade::hooks::call(glTextureSubImage2D);
	trampoline(texture, level, xoffset, yoffset, width, height, format, type, pixels);
}
void APIENTRY glTextureSubImage3D(GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, const void *pixels)
{

	static const auto trampoline = reshade::hooks::call(glTextureSubImage3D);
	trampoline(texture, level, xoffset, yoffset, zoffset, width, height, depth, format, type, pixels);
}
void APIENTRY glCompressedTextureSubImage1D(GLuint texture, GLint level, GLint xoffset, GLsizei width, GLenum format, GLsizei imageSize, const void *data)
{

	static const auto trampoline = reshade::hooks::call(glCompressedTextureSubImage1D);
	trampoline(texture, level, xoffset, width, format, imageSize, data);
}
void APIENTRY glCompressedTextureSubImage2D(GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLsizei imageSize, const void *data)
{

	static const auto trampoline = reshade::hooks::call(glCompressedTextureSubImage2D);
	trampoline(texture, level, xoffset, yoffset, width, height, format, imageSize, data);
}
void APIENTRY glCompressedTextureSubImage3D(GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLsizei imageSize, const void *data)
{

	static const auto trampoline = reshade::hooks::call(glCompressedTextureSubImage3D);
	trampoline(texture, level, xoffset, yoffset, zoffset, width, height, depth, format, imageSize, data);
}

void APIENTRY glCopyTextureSubImage1D(GLuint texture, GLint level, GLint xoffset, GLint x, GLint y, GLsizei width)
{

	static const auto trampoline = reshade::hooks::call(glCopyTextureSubImage1D);
	trampoline(texture, level, xoffset, x, y, width);
}
void APIENTRY glCopyTextureSubImage2D(GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height)
{

	static const auto trampoline = reshade::hooks::call(glCopyTextureSubImage2D);
	trampoline(texture, level, xoffset, yoffset, x, y, width, height);
}
void APIENTRY glCopyTextureSubImage3D(GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLint x, GLint y, GLsizei width, GLsizei height)
{

	static const auto trampoline = reshade::hooks::call(glCopyTextureSubImage3D);
	trampoline(texture, level, xoffset, yoffset, zoffset, x, y, width, height);
}

auto APIENTRY glMapNamedBuffer(GLuint buffer, GLenum access) -> void *
{
	static const auto trampoline = reshade::hooks::call(glMapNamedBuffer);
	void *result = trampoline(buffer, access);


	return result;
}
auto APIENTRY glMapNamedBufferRange(GLuint buffer, GLintptr offset, GLsizeiptr length, GLenum access) -> void *
{
	static const auto trampoline = reshade::hooks::call(glMapNamedBufferRange);
	void *result = trampoline(buffer, offset, length, access);


	return result;
}
void APIENTRY glUnmapNamedBuffer(GLuint buffer)
{

	static const auto trampoline = reshade::hooks::call(glUnmapNamedBuffer);
	trampoline(buffer);
}

void APIENTRY glCopyNamedBufferSubData(GLuint readBuffer, GLuint writeBuffer, GLintptr readOffset, GLintptr writeOffset, GLsizeiptr size)
{

	static const auto trampoline = reshade::hooks::call(glCopyNamedBufferSubData);
	trampoline(readBuffer, writeBuffer, readOffset, writeOffset, size);
}

void APIENTRY glNamedRenderbufferStorage(GLuint renderbuffer, GLenum internalformat, GLsizei width, GLsizei height)
{
	static const auto trampoline = reshade::hooks::call(glNamedRenderbufferStorage);
	trampoline(renderbuffer, internalformat, width, height);
}
void APIENTRY glNamedRenderbufferStorageMultisample(GLuint renderbuffer, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height)
{
	static const auto trampoline = reshade::hooks::call(glNamedRenderbufferStorageMultisample);

	trampoline(renderbuffer, samples, internalformat, width, height);
}

void APIENTRY glClearNamedFramebufferiv(GLuint framebuffer, GLenum buffer, GLint drawbuffer, const GLint *value)
{

	static const auto trampoline = reshade::hooks::call(glClearNamedFramebufferiv);
	trampoline(framebuffer, buffer, drawbuffer, value);
}
void APIENTRY glClearNamedFramebufferuiv(GLuint framebuffer, GLenum buffer, GLint drawbuffer, const GLuint *value)
{

	static const auto trampoline = reshade::hooks::call(glClearNamedFramebufferuiv);
	trampoline(framebuffer, buffer, drawbuffer, value);
}
void APIENTRY glClearNamedFramebufferfv(GLuint framebuffer, GLenum buffer, GLint drawbuffer, const GLfloat *value)
{

	static const auto trampoline = reshade::hooks::call(glClearNamedFramebufferfv);
	trampoline(framebuffer, buffer, drawbuffer, value);
}
void APIENTRY glClearNamedFramebufferfi(GLuint framebuffer, GLenum buffer, GLint drawbuffer, GLfloat depth, GLint stencil)
{

	static const auto trampoline = reshade::hooks::call(glClearNamedFramebufferfi);
	trampoline(framebuffer, buffer, drawbuffer, depth, stencil);
}

void APIENTRY glBlitNamedFramebuffer(GLuint readFramebuffer, GLuint drawFramebuffer, GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter)
{

	static const auto trampoline = reshade::hooks::call(glBlitNamedFramebuffer);
	trampoline(readFramebuffer, drawFramebuffer, srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1, mask, filter);
}

void APIENTRY glGenerateTextureMipmap(GLuint texture)
{

	static const auto trampoline = reshade::hooks::call(glGenerateTextureMipmap);
	trampoline(texture);
}

void APIENTRY glBindTextureUnit(GLuint unit, GLuint texture)
{
	static const auto trampoline = reshade::hooks::call(glBindTextureUnit);
	trampoline(unit, texture);

}
#endif

void APIENTRY glBindProgramARB(GLenum target, GLuint program)
{
	static const auto trampoline = reshade::hooks::call(glBindProgramARB);
	trampoline(target, program);

}
void APIENTRY glProgramStringARB(GLenum target, GLenum format, GLsizei length, const GLvoid *string)
{
	static const auto trampoline = reshade::hooks::call(glProgramStringARB);
	trampoline(target, format, length, string);
}
void APIENTRY glDeleteProgramsARB(GLsizei n, const GLuint *programs)
{

	static const auto trampoline = reshade::hooks::call(glDeleteProgramsARB);
	trampoline(n, programs);
}

void APIENTRY glBindFramebufferEXT(GLenum target, GLuint framebuffer)
{
	static const auto trampoline = reshade::hooks::call(glBindFramebufferEXT);
	trampoline(target, framebuffer);

}

void APIENTRY glBindMultiTextureEXT(GLenum texunit, GLenum target, GLuint texture)
{
	static const auto trampoline = reshade::hooks::call(glBindMultiTextureEXT);
	trampoline(texunit, target, texture);

}
