//
//  VROPhysicsBody.m
//  ViroRenderer
//
//  Copyright © 2017 Viro Media. All rights reserved.
//

#include "VROPhysicsBody.h"
#include "VRONode.h"
#include "VROPhysicsMotionState.h"
#include <btBulletDynamicsCommon.h>
const std::string VROPhysicsBody::kDynamicTag = "dynamic";
const std::string VROPhysicsBody::kKinematicTag = "kinematic";
const std::string VROPhysicsBody::kStaticTag = "static";

VROPhysicsBody::VROPhysicsBody(std::shared_ptr<VRONode> node, VROPhysicsBody::VROPhysicsBodyType type,
                                         float mass, std::shared_ptr<VROPhysicsShape> shape) {
    if (type == VROPhysicsBody::VROPhysicsBodyType::Dynamic && mass == 0) {
        pwarn("Attempted to incorrectly set 0 mass for a Dynamic body type! Defaulting to 1kg mass.");
        mass = 1;
    } else if (type != VROPhysicsBody::VROPhysicsBodyType::Dynamic && mass !=0) {
        pwarn("Attempted to incorrectly set mass for a static or kinematic body type! Defaulting to 0kg mass.");
        mass = 0;
    }

    // Create the underlying Bullet Rigid body with a bullet shape if possible.
    // If no VROPhysicsShape is provided, one is inferred during a computePhysics pass.
    btVector3 uniformInertia = btVector3(1,1,1);
    btCollisionShape *collisionShape = shape == nullptr ? nullptr : shape->getBulletShape();
    btRigidBody::btRigidBodyConstructionInfo groundRigidBodyCI(mass , nullptr, collisionShape, uniformInertia);

    // Notify renderer (primarily the physics world) that this VROPhysicsBody has changed.
    _needsBulletUpdate = true;
    _rigidBody = new btRigidBody(groundRigidBodyCI);

    // Set appropriate collision flags for the corresponding VROPhysicsBodyType
    if (type == VROPhysicsBody::VROPhysicsBodyType::Kinematic) {
        _rigidBody->setCollisionFlags(_rigidBody->getCollisionFlags() | btCollisionObject::CF_KINEMATIC_OBJECT);
        _rigidBody->setActivationState(DISABLE_DEACTIVATION);
    } else if (type == VROPhysicsBody::VROPhysicsBodyType::Static) {
        _rigidBody->setCollisionFlags(_rigidBody->getCollisionFlags() | btCollisionObject::CF_STATIC_OBJECT);
    }

    // Default physics properties
    _enableSimulation = true;
    _useGravity = true;
    _w_node = node;
    _shape = shape;
    _type = type;
    _mass = mass;
    _inertia = VROVector3f({uniformInertia.x(), uniformInertia.y(), uniformInertia.z()});
}

VROPhysicsBody::~VROPhysicsBody() {
    btMotionState *state = _rigidBody->getMotionState();
    if (state != nullptr) {
        delete state;
    }
    delete _rigidBody;
}

btRigidBody* VROPhysicsBody::getBulletRigidBody() {
    return _rigidBody;
}

#pragma mark - RigidBody properties
void VROPhysicsBody::setMass(float mass) {
    if (_type != VROPhysicsBody::VROPhysicsBodyType::Dynamic) {
        pwarn("Attempted to incorrectly set mass for a static or kinematic body type!");
        return;
    }
    _mass = mass;
    _rigidBody->setMassProps(mass, {_inertia.x, _inertia.y, _inertia.z});
}

void VROPhysicsBody::setInertia(VROVector3f inertia) {
    if (_type != VROPhysicsBody::VROPhysicsBodyType::Dynamic) {
        pwarn("Attempted to incorrectly set inertia for a static or kinematic body type!");
        return;
    }
    _inertia = inertia;
    _rigidBody->setMassProps(_mass, {inertia.x, inertia.y, inertia.z});
}


void VROPhysicsBody::setRestitution(float restitution) {
    _rigidBody->setRestitution(restitution);
}

void VROPhysicsBody::setFriction(float friction) {
    _rigidBody->setFriction(friction);
}

void VROPhysicsBody::setUseGravity(bool useGravity) {
    _useGravity = useGravity;
}

bool VROPhysicsBody::getUseGravity() {
    return _useGravity;
}

void VROPhysicsBody::setPhysicsShape(std::shared_ptr<VROPhysicsShape> shape) {
    if (_shape == shape) {
        return;
    }
    _shape = shape;

    // Bullet needs to refresh it's underlying object when changing its physics shape.
    _needsBulletUpdate = true;
}

void VROPhysicsBody::setIsSimulated(bool enabled) {
    if (_enableSimulation == enabled){
        return;
    }
    _enableSimulation = enabled;
    _needsBulletUpdate = true;
}

bool VROPhysicsBody::getIsSimulated() {
    return _enableSimulation;
}

#pragma mark - Transfomation Updates
bool VROPhysicsBody::needsBulletUpdate() {
    return _needsBulletUpdate;
}

void VROPhysicsBody::updateBulletRigidBody() {
    if (!_needsBulletUpdate){
        return;
    }

    // Update the rigid body to the latest world transform.
    std::shared_ptr<VRONode> node = _w_node.lock();
    if (node){
        if (_rigidBody->getMotionState() == nullptr) {
            VROPhysicsMotionState *motionState = new VROPhysicsMotionState(shared_from_this());
            _rigidBody->setMotionState(motionState);
        }

        btTransform transform;
        getWorldTransform(transform);
        _rigidBody->setWorldTransform(transform);
    }

    // Update the rigid body to reflect the latest VROPhysicsShape.
    // If shape is not defined, we attempt to infer the shape from the node's geometry.
    if (_shape == nullptr && node && node->getGeometry()){
        _shape = std::make_shared<VROPhysicsShape>(node->getGeometry());
    } else if (_shape == nullptr) {
        pwarn("No collision shape detected for this rigidbody... defaulting to basic box shape.");
        std::vector<float> params = {1, 1, 1};
        _shape = std::make_shared<VROPhysicsShape>(VROPhysicsShape::VROShapeType::Box, params);
    }
    _rigidBody->setCollisionShape(_shape->getBulletShape());

    // Set flag to false indicating that the modifications has applied to the bullet rigid body.
    _needsBulletUpdate = false;
}

void VROPhysicsBody::getWorldTransform(btTransform& centerOfMassWorldTrans ) const {
    std::shared_ptr<VRONode> node = _w_node.lock();
    if (!node){
        return;
    }

    VROVector3f pos = node->getComputedPosition();
    VROQuaternion rot = VROQuaternion(node->getComputedRotation());
    centerOfMassWorldTrans =
            btTransform(btQuaternion(rot.X, rot.Y, rot.Z, rot.W), btVector3(pos.x, pos.y, pos.z));
}

void VROPhysicsBody::setWorldTransform(const btTransform& centerOfMassWorldTrans) {
    std::shared_ptr<VRONode> node = _w_node.lock();
    if (!node){
        return;
    }

    btQuaternion rot = centerOfMassWorldTrans.getRotation();
    btVector3 pos = centerOfMassWorldTrans.getOrigin();
    node->setWorldTransform({pos.getX(), pos.getY(), pos.getZ()},
                            VROQuaternion(rot.x(), rot.y(), rot.z(), rot.w()));
}

#pragma mark - Forces
void VROPhysicsBody::applyCenteralForce(VROVector3f force) {
    _rigidBody->applyCentralForce(btVector3(force.x, force.y, force.z));
}

void VROPhysicsBody::applyCenteralImpulse(VROVector3f impulse) {
    _rigidBody->applyCentralImpulse(btVector3(impulse.x, impulse.y, impulse.z));
}

void VROPhysicsBody::applyTorque(VROVector3f torque) {
    _rigidBody->applyTorque(btVector3(torque.x, torque.y, torque.z));
}

void VROPhysicsBody::applyTorqueImpulse(VROVector3f impulse) {
    _rigidBody->applyTorqueImpulse(btVector3(impulse.x, impulse.y, impulse.z));
}