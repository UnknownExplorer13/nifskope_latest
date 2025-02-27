/***** BEGIN LICENSE BLOCK *****

BSD License

Copyright (c) 2005-2015, NIF File Format Library and Tools
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. The name of the NIF File Format Library and Tools project may not be
   used to endorse or promote products derived from this software
   without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

***** END LICENCE BLOCK *****/

#ifndef GLMESH_H
#define GLMESH_H

#include "gl/glshape.h" // Inherited

//! @file glmesh.h Mesh

//! A mesh
class Mesh : public Shape
{

public:
	Mesh( Scene * s, const QModelIndex & b ) : Shape( s, b ) { }

	// Node

	void transformShapes() override;

	void drawShapes( NodeList * secondPass = nullptr ) override;
	void drawSelection() const override;

	BoundSphere bounds() const override;

	QString textStats() const override; // TODO (Gavrant): move to Shape

	// end Node

	// Shape

	void drawVerts() const;
	QModelIndex vertexAt( int ) const override;
	QModelIndex triangleAt( int ) const override;
	void updateLodLevel() override;

protected:
	void updateImpl( const NifModel * nif, const QModelIndex & index ) override;
	void addBoneWeight( int vertexNum, int boneNum, float weight );
	void updateData( const NifModel * nif ) override;

	void updateData_NiMesh( const NifModel * nif );
	void updateData_NiTriShape( const NifModel * nif );
};

#endif
