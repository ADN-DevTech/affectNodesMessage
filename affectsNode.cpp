//-
// ==========================================================================
// Copyright 1995,2006,2008 Autodesk, Inc. All rights reserved.
//
// Use of this software is subject to the terms of the Autodesk
// license agreement provided at the time of installation or download,
// or which otherwise accompanies this software in either electronic
// or hard copy form.
// ==========================================================================
//

#include <maya/MPxNode.h> 

#include <maya/MFnNumericAttribute.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MFnPlugin.h>

#include <maya/MString.h> 
#include <maya/MTypeId.h> 
#include <maya/MPlug.h>

#include <maya/MFnCamera.h>
#include <maya/MNodeMessage.h>
#include <maya/MFloatMatrix.h>
#include <maya/MGlobal.h>
#include <maya/MFnDagNode.h>
#include <maya/MDagPath.h>

#include <maya/MDataBlock.h>
#include <maya/MDataHandle.h>
#include <maya/MFnMatrixAttribute.h>
#include <maya/MFnMessageAttribute.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MPlugArray.h>
#include <maya/MDagMessage.h>

#include <maya/MSelectionList.h>


#define PROJECTION_MATRIX_LN "projectionMatrix"
#define PROJECTION_MATRIX_SN "pm"

#define INPUT_CAMERA_LN	"inputCamera"
#define INPUT_CAMERA_SN	"ic"

#define DIRTY_DUMMY_LN "dirtyDummy"
#define DIRTY_DUMMY_SN "dd"


// setup with following mel command in a new scene:
// createNode affects
// connectAttr perspShape.msg affects1.ic

class affects : public MPxNode
{
public:
						affects();
	virtual				~affects(); 

	virtual MStatus		compute( const MPlug& plug, MDataBlock& data );
	

	virtual MStatus		connectionMade( const MPlug& plug, const MPlug& otherPlug, bool asSrc ) override;
	virtual MStatus		connectionBroken( const MPlug& plug, const MPlug& otherPlug, bool asSrc ) override;

	static  void*		creator();
	static  MStatus		initialize();

	static	MTypeId		id;				// The IFF type id

	static	MObject		projectionMatrix;
	static  MObject		inputCamera;
	static	MObject		dirtyDummy;

private:
	MCallbackId cameraCallbackId;
	MCallbackId worldMatrixCallbackId;

	MObject cameraNode;
	bool isCameraSet;
	
	MFloatMatrix projectionMatrixValue;

	void setProjectionMatrixDirty();

	friend void cameraNodePlugDirty(MObject &node, MPlug &plug, void *clientData);

	friend void cameraWorldMatrixChangeCallback(MObject &transformNode, 
												MDagMessage::MatrixModifiedFlags &modified,
												void *userData);

};

MObject affects::projectionMatrix;
MObject affects::inputCamera;
MObject	affects::dirtyDummy;

MTypeId affects::id( 0x80028 );

affects::affects():isCameraSet(0) {}
affects::~affects() {
	if(isCameraSet)
	{
		MNodeMessage::removeCallback(cameraCallbackId);
		MDagMessage::removeCallback(worldMatrixCallbackId);
		isCameraSet = false;
	}
}

// Mark dummy attribute dirty to apply affect attributes
void affects::setProjectionMatrixDirty()
{
	auto dirtyPlug = MPlug(thisMObject(), dirtyDummy);
	bool lastValue;
	dirtyPlug.getValue(lastValue);
	dirtyPlug.setValue(!lastValue);
}

// Update projection matrix in the compute
MStatus affects::compute( const MPlug& plug, MDataBlock& data )
{
	if(plug == projectionMatrix)
	{
		MGlobal::displayInfo("Updating projection matrix in compute");
		auto handle = data.outputValue(projectionMatrix);
		handle.setMFloatMatrix(projectionMatrixValue);
		handle.setClean();
	}
	return MStatus::kSuccess;
}

// Create a node plug dirty callback on the camera when connection is made
MStatus affects::connectionMade( const MPlug& plug, const MPlug& otherPlug, bool asSrc )
{
	if(plug == inputCamera)
	{
		MObject otherNode = otherPlug.node();
	
		// If is from camera.message
		// It must be connected from cameraShape, otherwise won't work.
		if(otherNode.hasFn(MFn::kCamera))
		{
			MGlobal::displayInfo("Connection made.");
		
			isCameraSet = true;
			cameraNode = otherNode;

			MFnCamera fnCam(cameraNode);			
			projectionMatrixValue = fnCam.projectionMatrix();					
			setProjectionMatrixDirty();
			cameraCallbackId = MNodeMessage::addNodeDirtyPlugCallback(cameraNode, cameraNodePlugDirty, this);

			// Add world transform matrix
			MSelectionList sel;
			sel.add(fnCam.fullPathName());
			MDagPath dagPath;
			sel.getDagPath(0,dagPath);
			worldMatrixCallbackId = MDagMessage::addWorldMatrixModifiedCallback(dagPath,cameraWorldMatrixChangeCallback,this);
			
			return MStatus::kSuccess;
		}
	}
	return MPxNode::connectionMade(plug, otherPlug, asSrc);
}

// Remove callbacks when connection is broken.
MStatus affects::connectionBroken( const MPlug& plug, const MPlug& otherPlug, bool asSrc )
{
	if(plug == inputCamera)
	{
		MObject otherNode = otherPlug.node();
		// If is from cameraShape.message
		
		if(otherNode.hasFn(MFn::kCamera))
		{
			MGlobal::displayInfo("Connection breaks.");
			if(isCameraSet)
			{
				isCameraSet = false;
				cameraNode = otherNode;

				MFnCamera fnCam(cameraNode);
				projectionMatrixValue = fnCam.projectionMatrix();		
				
				setProjectionMatrixDirty();

				MNodeMessage::removeCallback(cameraCallbackId);
				MDagMessage::removeCallback(worldMatrixCallbackId);
			}
			return MStatus::kSuccess;
		}
	}
	return MPxNode::connectionBroken(plug, otherPlug, asSrc);
}


void* affects::creator()
{
	return( new affects() );
}

// Create message attribute and matrix attribute here.
MStatus affects::initialize()
{
	MFnMatrixAttribute matAttr;
	MFnMessageAttribute msgAttr;
	MFnNumericAttribute nAttr;

	// Use for projection matrix
	projectionMatrix = matAttr.create(PROJECTION_MATRIX_LN, PROJECTION_MATRIX_SN, MFnMatrixAttribute::kFloat);

	// Message attribute 
	inputCamera = msgAttr.create(INPUT_CAMERA_LN, INPUT_CAMERA_SN);

	// For attributeAffects
	dirtyDummy = nAttr.create(DIRTY_DUMMY_LN, DIRTY_DUMMY_SN, MFnNumericData::kBoolean);

	addAttribute(projectionMatrix);
	addAttribute(inputCamera);
	addAttribute(dirtyDummy);

	attributeAffects(dirtyDummy, projectionMatrix);

	return( MS::kSuccess );
}

// Node plug dirty change callback
void cameraNodePlugDirty(MObject &node, MPlug &plug, void *clientData)
{
	affects* affectNode = (affects*)clientData;

	if(affectNode->isCameraSet)
	{
		MGlobal::displayInfo("Updating projection matrix after camera shape changes.");
		MFnCamera fnCam(affectNode->cameraNode);

		//Cache the projection matrix value here. It will prevent potential DG evaluation issues.
		affectNode->projectionMatrixValue = fnCam.projectionMatrix();
		affectNode->setProjectionMatrixDirty();
	}	
}

//World matrix change callback
void cameraWorldMatrixChangeCallback(MObject &transformNode, MDagMessage::MatrixModifiedFlags &modified, void *userData)
{
	affects* affectNode = (affects*)userData;
	
	if(affectNode->isCameraSet)
	{
		MGlobal::displayInfo("Updating projection matrix after transform changes.");
		MFnCamera fnCam(affectNode->cameraNode);

		//Cache the projection matrix value here. It will prevent potential DG evaluation issues.
		affectNode->projectionMatrixValue = fnCam.projectionMatrix();
		affectNode->setProjectionMatrixDirty();
	}	
}

// These methods load and unload the plugin, registerNode registers the
// new node type with maya
//
MStatus initializePlugin( MObject obj )
{ 
	MStatus   status;
	MFnPlugin plugin( obj, PLUGIN_COMPANY , "6.0", "Any");

	status = plugin.registerNode( "affects", affects::id, affects::creator,
								  affects::initialize );
	if (!status) {
		status.perror("registerNode");
		return( status );
	}

	return( status );
}

MStatus uninitializePlugin( MObject obj)
{
	MStatus   status;
	MFnPlugin plugin( obj );

	status = plugin.deregisterNode( affects::id );
	if (!status) {
		status.perror("deregisterNode");
		return( status );
	}

	return( status );
}
