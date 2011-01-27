/*********************************************************************
 * Software License Agreement (BSD License)
 * 
 *  Copyright (c) 2008, Willow Garage, Inc.
 *  All rights reserved.
 * 
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 * 
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of the Willow Garage nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 * 
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *********************************************************************/

/** \author Ioan Sucan */

#include "collision_space/environmentODE.h"
#include <geometric_shapes/shape_operations.h>
#include <ros/console.h>
#include <boost/thread.hpp>
#include <cassert>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <map>

static int          ODEInitCount = 0;
static boost::mutex ODEInitCountLock;

static std::map<boost::thread::id, int> ODEThreadMap;
static boost::mutex                     ODEThreadMapLock;

static const unsigned int MAX_ODE_CONTACTS = 128;

collision_space::EnvironmentModelODE::EnvironmentModelODE(void) : EnvironmentModel()
{
  ODEInitCountLock.lock();
  if (ODEInitCount == 0)
  {
    ROS_DEBUG("Initializing ODE");
    dInitODE2(0);
  }
  ODEInitCount++;
  ODEInitCountLock.unlock();
    
  checkThreadInit();

  m_modelGeom.space = dSweepAndPruneSpaceCreate(0, dSAP_AXES_XZY);
}

collision_space::EnvironmentModelODE::~EnvironmentModelODE(void)
{
  freeMemory();
  ODEInitCountLock.lock();
  ODEInitCount--;
  if (ODEInitCount == 0)
  {
    ROS_DEBUG("Closing ODE");
    dCloseODE();
  }
  ODEInitCountLock.unlock();
}

void collision_space::EnvironmentModelODE::checkThreadInit(void) const
{
  boost::thread::id id = boost::this_thread::get_id();
  ODEThreadMapLock.lock();
  if (ODEThreadMap.find(id) == ODEThreadMap.end())
  {
    ODEThreadMap[id] = 1;
    ROS_DEBUG("Initializing new thread (%d total)", (int)ODEThreadMap.size());
    dAllocateODEDataForThread(dAllocateMaskAll);
  }
  ODEThreadMapLock.unlock();
}

void collision_space::EnvironmentModelODE::freeMemory(void)
{ 
  for (unsigned int j = 0 ; j < m_modelGeom.linkGeom.size() ; ++j)
    delete m_modelGeom.linkGeom[j];
  if (m_modelGeom.space)
    dSpaceDestroy(m_modelGeom.space);
  for (std::map<std::string, CollisionNamespace*>::iterator it = m_collNs.begin() ; it != m_collNs.end() ; ++it)
    delete it->second;
}

void collision_space::EnvironmentModelODE::setRobotModel(const planning_models::KinematicModel* model, 
                                                         const std::vector<std::string> &links, 
                                                         const std::map<std::string, double>& link_padding_map,
                                                         double default_padding,
                                                         double scale) 
{
  collision_space::EnvironmentModel::setRobotModel(model, links, link_padding_map, default_padding, scale);
  link_padding_map_ = link_padding_map;
  createODERobotModel();
}

const std::vector<const planning_models::KinematicModel::AttachedBodyModel*> collision_space::EnvironmentModelODE::getAttachedBodies(void) const 
{
  std::vector<const planning_models::KinematicModel::AttachedBodyModel*> ret_vec;

  const unsigned int n = m_modelGeom.linkGeom.size();    
  for (unsigned int i = 0 ; i < n ; ++i)
  {
    kGeom *kg = m_modelGeom.linkGeom[i];
	
    /* create new set of attached bodies */	
    const unsigned int nab = kg->link->getAttachedBodyModels().size();
    for (unsigned int k = 0 ; k < nab ; ++k)
    {
      ret_vec.push_back(kg->link->getAttachedBodyModels()[k]);
    }
  }
  return ret_vec;
}

const std::vector<const planning_models::KinematicModel::AttachedBodyModel*> collision_space::EnvironmentModelODE::getAttachedBodies(std::string link_name) const 
{
  std::vector<const planning_models::KinematicModel::AttachedBodyModel*> ret_vec;

  if(m_collisionLinkIndex.find(link_name) == m_collisionLinkIndex.end()) {
    return ret_vec;
  }

  unsigned int ind = m_collisionLinkIndex.find(link_name)->second;

  kGeom *kg = m_modelGeom.linkGeom[ind];
	
  /* create new set of attached bodies */	
  const unsigned int nab = kg->link->getAttachedBodyModels().size();
  for (unsigned int k = 0 ; k < nab ; ++k)
  {
    ret_vec.push_back(kg->link->getAttachedBodyModels()[k]);
  }
  return ret_vec;
}

void collision_space::EnvironmentModelODE::createODERobotModel()
{
  for (unsigned int i = 0 ; i < m_collisionLinks.size() ; ++i)
  {
    /* skip this link if we have no geometry or if the link
       name is not specified as enabled for collision
       checking */
    const planning_models::KinematicModel::LinkModel *link = m_robotModel->getLinkModel(m_collisionLinks[i]);
    if (!link || !link->getLinkShape())
      continue;
	
    kGeom *kg = new kGeom();
    kg->link = link;
    kg->enabled = true;
    kg->index = i;
    double padd = m_robotPadd;
    if(link_padding_map_.find(link->getName()) != link_padding_map_.end()) {
      padd = link_padding_map_.find(link->getName())->second;
    }
    ROS_DEBUG_STREAM("Link " << link->getName() << " padding " << padd);
    dGeomID g = createODEGeom(m_modelGeom.space, m_modelGeom.storage, link->getLinkShape(), m_robotScale, padd);
    assert(g);
    dGeomSetData(g, reinterpret_cast<void*>(kg));
    kg->geom.push_back(g);
    const std::vector<planning_models::KinematicModel::AttachedBodyModel*>& attached_bodies = link->getAttachedBodyModels();
    for (unsigned int j = 0 ; j < attached_bodies.size() ; ++j)
    {
      for(unsigned int k = 0; k < attached_bodies[j]->getShapes().size(); k++) {
        padd = m_robotPadd;
        if(link_padding_map_.find(attached_bodies[j]->getName()) != link_padding_map_.end()) {
          padd = link_padding_map_.find(attached_bodies[j]->getName())->second;
        } else if (link_padding_map_.find("attached") != link_padding_map_.end()) {
          padd = link_padding_map_.find("attached")->second;
        }        
        dGeomID ga = createODEGeom(m_modelGeom.space, m_modelGeom.storage, attached_bodies[j]->getShapes()[k], m_robotScale, padd);
        assert(ga);
        dGeomSetData(ga, reinterpret_cast<void*>(kg));
        kg->geom.push_back(ga);
        kg->geomAttachedBodyMap[ga] = j+1;
      }
    }
    m_modelGeom.linkGeom.push_back(kg);
  } 
  updateAllowedTouch();    
}

dGeomID collision_space::EnvironmentModelODE::createODEGeom(dSpaceID space, ODEStorage &storage, const shapes::StaticShape *shape)
{
  dGeomID g = NULL;
  switch (shape->type)
  {
  case shapes::PLANE:
    {
      const shapes::Plane *p = static_cast<const shapes::Plane*>(shape);
      g = dCreatePlane(space, p->a, p->b, p->c, p->d);
    }
    break;
  default:
    break;
  }
  return g;
}

dGeomID collision_space::EnvironmentModelODE::createODEGeom(dSpaceID space, ODEStorage &storage, const shapes::Shape *shape, double scale, double padding)
{
  dGeomID g = NULL;
  switch (shape->type)
  {
  case shapes::SPHERE:
    {
      g = dCreateSphere(space, static_cast<const shapes::Sphere*>(shape)->radius * scale + padding);
    }
    break;
  case shapes::BOX:
    {
      const double *size = static_cast<const shapes::Box*>(shape)->size;
      g = dCreateBox(space, size[0] * scale + padding * 2.0, size[1] * scale + padding * 2.0, size[2] * scale + padding * 2.0);
    }	
    break;
  case shapes::CYLINDER:
    {
      g = dCreateCylinder(space, static_cast<const shapes::Cylinder*>(shape)->radius * scale + padding,
                          static_cast<const shapes::Cylinder*>(shape)->length * scale + padding * 2.0);
    }
    break;
  case shapes::MESH:
    {
      const shapes::Mesh *mesh = static_cast<const shapes::Mesh*>(shape);
      if (mesh->vertexCount > 0 && mesh->triangleCount > 0)
      {		
        // copy indices for ODE
        int icount = mesh->triangleCount * 3;
        dTriIndex *indices = new dTriIndex[icount];
        for (int i = 0 ; i < icount ; ++i)
          indices[i] = mesh->triangles[i];
		
        // copt vertices for ODE
        double *vertices = new double[mesh->vertexCount* 3];
        double sx = 0.0, sy = 0.0, sz = 0.0;
        for (unsigned int i = 0 ; i < mesh->vertexCount ; ++i)
        {
          unsigned int i3 = i * 3;
          vertices[i3] = mesh->vertices[i3];
          vertices[i3 + 1] = mesh->vertices[i3 + 1];
          vertices[i3 + 2] = mesh->vertices[i3 + 2];
          sx += vertices[i3];
          sy += vertices[i3 + 1];
          sz += vertices[i3 + 2];
        }
        // the center of the mesh
        sx /= (double)mesh->vertexCount;
        sy /= (double)mesh->vertexCount;
        sz /= (double)mesh->vertexCount;

        // scale the mesh
        for (unsigned int i = 0 ; i < mesh->vertexCount ; ++i)
        {
          unsigned int i3 = i * 3;
		    
          // vector from center to the vertex
          double dx = vertices[i3] - sx;
          double dy = vertices[i3 + 1] - sy;
          double dz = vertices[i3 + 2] - sz;
		    
          // length of vector
          //double norm = sqrt(dx * dx + dy * dy + dz * dz);
		    
          double ndx = ((dx > 0) ? dx+padding : dx-padding);
          double ndy = ((dy > 0) ? dy+padding : dy-padding);
          double ndz = ((dz > 0) ? dz+padding : dz-padding);

          // the new distance of the vertex from the center
          //double fact = scale + padding/norm;
          vertices[i3] = sx + ndx; //dx * fact;
          vertices[i3 + 1] = sy + ndy; //dy * fact;
          vertices[i3 + 2] = sz + ndz; //dz * fact;		    
        }
		
        dTriMeshDataID data = dGeomTriMeshDataCreate();
        dGeomTriMeshDataBuildDouble(data, vertices, sizeof(double) * 3, mesh->vertexCount, indices, icount, sizeof(dTriIndex) * 3);
        g = dCreateTriMesh(space, data, NULL, NULL, NULL);
        unsigned int p = storage.mesh.size();
        storage.mesh.resize(p + 1);
        storage.mesh[p].vertices = vertices;
        storage.mesh[p].indices = indices;
        storage.mesh[p].data = data;
        storage.mesh[p].nVertices = mesh->vertexCount;
        storage.mesh[p].nIndices = icount;
      }
    }
	
  default:
    break;
  }
  return g;
}

void collision_space::EnvironmentModelODE::updateGeom(dGeomID geom,  const btTransform &pose) const
{
  btVector3 pos = pose.getOrigin();
  dGeomSetPosition(geom, pos.getX(), pos.getY(), pos.getZ());
  btQuaternion quat = pose.getRotation();
  dQuaternion q; 
  q[0] = quat.getW(); q[1] = quat.getX(); q[2] = quat.getY(); q[3] = quat.getZ();
  dGeomSetQuaternion(geom, q);
}

void collision_space::EnvironmentModelODE::updateAttachedBodies()
{
  updateAttachedBodies(link_padding_map_);
}

void collision_space::EnvironmentModelODE::updateAttachedBodies(const std::map<std::string, double>& link_padding_map)
{
  for (unsigned int i = 0 ; i < m_modelGeom.linkGeom.size() ; ++i) {
    kGeom *kg = m_modelGeom.linkGeom[i];
  
    kg->geomAttachedBodyMap.clear();
    /* clear previosly attached bodies */
    for (unsigned int j = 1 ; j < kg->geom.size() ; ++j)
      dGeomDestroy(kg->geom[j]);
    kg->geom.resize(1);
	
    /* create new set of attached bodies */
    const std::vector<planning_models::KinematicModel::AttachedBodyModel*>& attached_bodies = kg->link->getAttachedBodyModels();
    for (unsigned int j = 0 ; j < attached_bodies.size(); ++j) {
      ROS_DEBUG_STREAM("Updating attached body " << attached_bodies[j]->getName());
      for(unsigned int k = 0; k < attached_bodies[j]->getShapes().size(); k++) {
        double padd = m_robotPadd;
        if(link_padding_map.find(attached_bodies[j]->getName()) != link_padding_map.end()) {
          padd = link_padding_map.find(attached_bodies[j]->getName())->second;
        } else if (link_padding_map.find("attached") != link_padding_map.end()) {
          padd = link_padding_map.find("attached")->second;
        }
        ROS_DEBUG_STREAM("Setting padding for attached body " << attached_bodies[j]->getName() << " to " << padd);
        dGeomID ga = createODEGeom(m_modelGeom.space, m_modelGeom.storage, attached_bodies[j]->getShapes()[k], m_robotScale, padd);
        assert(ga);
        dGeomSetData(ga, reinterpret_cast<void*>(kg));
        kg->geom.push_back(ga);
        kg->geomAttachedBodyMap[ga] = j+1;
      }
    }
  }
  updateAllowedTouch();
}

void collision_space::EnvironmentModelODE::updateRobotModel(const planning_models::KinematicState* state)
{ 
  const unsigned int n = m_modelGeom.linkGeom.size();
    
  for (unsigned int i = 0 ; i < n ; ++i) {
    const planning_models::KinematicState::LinkState* link_state = state->getLinkState(m_modelGeom.linkGeom[i]->link->getName());
    if(link_state == NULL) {
      ROS_WARN_STREAM("No link state for link " << m_modelGeom.linkGeom[i]->link->getName());
      continue;
    }
    updateGeom(m_modelGeom.linkGeom[i]->geom[0], link_state->getGlobalCollisionBodyTransform());
    const std::vector<planning_models::KinematicState::AttachedBodyState*>& attached_bodies = link_state->getAttachedBodyStateVector();
    for (unsigned int j = 0 ; j < attached_bodies.size(); ++j) {
      for(unsigned int k = 0; k < attached_bodies[j]->getGlobalCollisionBodyTransforms().size(); k++) {
        updateGeom(m_modelGeom.linkGeom[i]->geom[k + 1], attached_bodies[j]->getGlobalCollisionBodyTransforms()[k]);
      }
    }
  }    
}

void collision_space::EnvironmentModelODE::setRobotLinkPadding(const std::map<std::string, double>& new_link_padding) {
  
  for(unsigned int i = 0; i < m_modelGeom.linkGeom.size(); i++) {

    kGeom *kg = m_modelGeom.linkGeom[i];

    if(new_link_padding.find(kg->link->getName()) != new_link_padding.end()) {
      if(link_padding_map_.find(kg->link->getName()) == link_padding_map_.end()) {
        ROS_WARN_STREAM("No existing padding for object " << kg->link->getName());
        continue;
      }
      double new_padding = new_link_padding.find(kg->link->getName())->second;
      if(link_padding_map_[kg->link->getName()] == new_padding) {
        //same as old
        continue;
      }
      const planning_models::KinematicModel::LinkModel *link = m_robotModel->getLinkModel(m_collisionLinks[i]);
      if (!link || !link->getLinkShape()) {
        ROS_WARN_STREAM("Can't get kinematic model for link " << link->getName() << " to make new padding");
        continue;
      }
      ROS_DEBUG_STREAM("Setting padding for link " << kg->link->getName() << " from " << link_padding_map_[kg->link->getName()] 
                      << " to " << new_padding);
      //otherwise we clear out the data associated with the old one
      for (unsigned int j = 0 ; j < kg->geom.size() ; ++j) {
        dGeomDestroy(kg->geom[j]);
      }
      kg->geom.clear();
      dGeomID g = createODEGeom(m_modelGeom.space, m_modelGeom.storage, link->getLinkShape(), m_robotScale, new_padding);
      assert(g);
      dGeomSetData(g, reinterpret_cast<void*>(kg));
      kg->geom.push_back(g);
    }
  }
  //this does all the work
  updateAttachedBodies(new_link_padding);  

  //updating altered map
  collision_space::EnvironmentModel::setRobotLinkPadding(new_link_padding);
}

void collision_space::EnvironmentModelODE::revertRobotLinkPadding() {
  for(unsigned int i = 0; i < m_modelGeom.linkGeom.size(); i++) {
    
    kGeom *kg = m_modelGeom.linkGeom[i];

    if(altered_link_padding_.find(kg->link->getName()) != altered_link_padding_.end()) {
      if(link_padding_map_.find(kg->link->getName()) == link_padding_map_.end()) {
        ROS_WARN_STREAM("No initial padding for object " << kg->link->getName());
        continue;
      }
      double old_padding = link_padding_map_[kg->link->getName()];
      if(altered_link_padding_[kg->link->getName()] == old_padding) {
        //same as old
        continue;
      }
      const planning_models::KinematicModel::LinkModel *link = m_robotModel->getLinkModel(m_collisionLinks[i]);
      if (!link || !link->getLinkShape()) {
        ROS_WARN_STREAM("Can't get kinematic model for link " << link->getName() << " to revert to old padding");
        continue;
      }
      //otherwise we clear out the data associated with the old one
      for (unsigned int j = 0 ; j < kg->geom.size() ; ++j) {
        dGeomDestroy(kg->geom[j]);
      }
      ROS_DEBUG_STREAM("Reverting padding for link " << kg->link->getName() << " from " << altered_link_padding_[kg->link->getName()]
                      << " to " << old_padding);
      kg->geom.clear();
      dGeomID g = createODEGeom(m_modelGeom.space, m_modelGeom.storage, link->getLinkShape(), m_robotScale, old_padding);
      assert(g);
      dGeomSetData(g, reinterpret_cast<void*>(kg));
      kg->geom.push_back(g);
    }
  }
  //will revert to whatever is in link_padding
  updateAttachedBodies(); 
  
  //clears altered map
  collision_space::EnvironmentModel::revertRobotLinkPadding();
} 

bool collision_space::EnvironmentModelODE::ODECollide2::empty(void) const
{
  return m_geomsX.empty();
}

void collision_space::EnvironmentModelODE::ODECollide2::registerSpace(dSpaceID space)
{
  int n = dSpaceGetNumGeoms(space);
  for (int i = 0 ; i < n ; ++i)
    registerGeom(dSpaceGetGeom(space, i));
}

void collision_space::EnvironmentModelODE::ODECollide2::unregisterGeom(dGeomID geom)
{
  setup();
    
  Geom g;
  g.id = geom;
  dGeomGetAABB(geom, g.aabb);
    
  Geom *found = NULL;
    
  std::vector<Geom*>::iterator posStart1 = std::lower_bound(m_geomsX.begin(), m_geomsX.end(), &g, SortByXTest());
  std::vector<Geom*>::iterator posEnd1   = std::upper_bound(posStart1, m_geomsX.end(), &g, SortByXTest());
  while (posStart1 < posEnd1)
  {
    if ((*posStart1)->id == geom)
    {
      found = *posStart1;
      m_geomsX.erase(posStart1);
      break;
    }
    ++posStart1;
  }

  std::vector<Geom*>::iterator posStart2 = std::lower_bound(m_geomsY.begin(), m_geomsY.end(), &g, SortByYTest());
  std::vector<Geom*>::iterator posEnd2   = std::upper_bound(posStart2, m_geomsY.end(), &g, SortByYTest());
  while (posStart2 < posEnd2)
  {
    if ((*posStart2)->id == geom)
    {
      assert(found == *posStart2);
      m_geomsY.erase(posStart2);
      break;
    }
    ++posStart2;
  }
    
  std::vector<Geom*>::iterator posStart3 = std::lower_bound(m_geomsZ.begin(), m_geomsZ.end(), &g, SortByZTest());
  std::vector<Geom*>::iterator posEnd3   = std::upper_bound(posStart3, m_geomsZ.end(), &g, SortByZTest());
  while (posStart3 < posEnd3)
  {
    if ((*posStart3)->id == geom)
    {
      assert(found == *posStart3);
      m_geomsZ.erase(posStart3);
      break;
    }
    ++posStart3;
  }
    
  assert(found);
  delete found;
}

void collision_space::EnvironmentModelODE::ODECollide2::registerGeom(dGeomID geom)
{
  Geom *g = new Geom();
  g->id = geom;
  dGeomGetAABB(geom, g->aabb);
  m_geomsX.push_back(g);
  m_geomsY.push_back(g);
  m_geomsZ.push_back(g);
  m_setup = false;
}
	
void collision_space::EnvironmentModelODE::ODECollide2::clear(void)
{
  for (unsigned int i = 0 ; i < m_geomsX.size() ; ++i)
    delete m_geomsX[i];
  m_geomsX.clear();
  m_geomsY.clear();
  m_geomsZ.clear();
  m_setup = false;
}

void collision_space::EnvironmentModelODE::ODECollide2::setup(void)
{
  if (!m_setup)
  {
    std::sort(m_geomsX.begin(), m_geomsX.end(), SortByXLow());
    std::sort(m_geomsY.begin(), m_geomsY.end(), SortByYLow());
    std::sort(m_geomsZ.begin(), m_geomsZ.end(), SortByZLow());
    m_setup = true;
  }	    
}

void collision_space::EnvironmentModelODE::ODECollide2::getGeoms(std::vector<dGeomID> &geoms) const
{
  geoms.resize(m_geomsX.size());
  for (unsigned int i = 0 ; i < geoms.size() ; ++i)
    geoms[i] = m_geomsX[i]->id;
}

void collision_space::EnvironmentModelODE::ODECollide2::checkColl(std::vector<Geom*>::const_iterator posStart, std::vector<Geom*>::const_iterator posEnd,
                                                                  Geom *g, void *data, dNearCallback *nearCallback) const
{
  /* posStart now identifies the first geom which has an AABB
     that could overlap the AABB of geom on the X axis. posEnd
     identifies the first one that cannot overlap. */
    
  while (posStart < posEnd)
  {
    /* if the boxes are not disjoint along Y, Z, check further */
    if (!((*posStart)->aabb[2] > g->aabb[3] ||
          (*posStart)->aabb[3] < g->aabb[2] ||
          (*posStart)->aabb[4] > g->aabb[5] ||
          (*posStart)->aabb[5] < g->aabb[4]))
      dSpaceCollide2(g->id, (*posStart)->id, data, nearCallback);
    posStart++;
  }
}

void collision_space::EnvironmentModelODE::ODECollide2::collide(dGeomID geom, void *data, dNearCallback *nearCallback) const
{
  static const int CUTOFF = 100;

  assert(m_setup);

  Geom g;
  g.id = geom;
  dGeomGetAABB(geom, g.aabb);
    
  std::vector<Geom*>::const_iterator posStart1 = std::lower_bound(m_geomsX.begin(), m_geomsX.end(), &g, SortByXTest());
  if (posStart1 != m_geomsX.end())
  {
    std::vector<Geom*>::const_iterator posEnd1 = std::upper_bound(posStart1, m_geomsX.end(), &g, SortByXTest());
    int                                d1      = posEnd1 - posStart1;
	
    /* Doing two binary searches on the sorted-by-y array takes
       log(n) time, which should be around 12 steps. Each step
       should be just a few ops, so a cut-off like 100 is
       appropriate. */
    if (d1 > CUTOFF)
    {
      std::vector<Geom*>::const_iterator posStart2 = std::lower_bound(m_geomsY.begin(), m_geomsY.end(), &g, SortByYTest());
      if (posStart2 != m_geomsY.end())
      {
        std::vector<Geom*>::const_iterator posEnd2 = std::upper_bound(posStart2, m_geomsY.end(), &g, SortByYTest());
        int                                d2      = posEnd2 - posStart2;
		
        if (d2 > CUTOFF)
        {
          std::vector<Geom*>::const_iterator posStart3 = std::lower_bound(m_geomsZ.begin(), m_geomsZ.end(), &g, SortByZTest());
          if (posStart3 != m_geomsZ.end())
          {
            std::vector<Geom*>::const_iterator posEnd3 = std::upper_bound(posStart3, m_geomsZ.end(), &g, SortByZTest());
            int                                d3      = posEnd3 - posStart3;
            if (d3 > CUTOFF)
            {
              if (d3 <= d2 && d3 <= d1)
                checkColl(posStart3, posEnd3, &g, data, nearCallback);
              else
                if (d2 <= d3 && d2 <= d1)
                  checkColl(posStart2, posEnd2, &g, data, nearCallback);
                else
                  checkColl(posStart1, posEnd1, &g, data, nearCallback);
            }
            else
              checkColl(posStart3, posEnd3, &g, data, nearCallback);   
          }
        }
        else
          checkColl(posStart2, posEnd2, &g, data, nearCallback);   
      }
    }
    else 
      checkColl(posStart1, posEnd1, &g, data, nearCallback);
  }
}

namespace collision_space
{

void nearCallbackFn(void *data, dGeomID o1, dGeomID o2)
{
  EnvironmentModelODE::CollisionData *cdata = reinterpret_cast<EnvironmentModelODE::CollisionData*>(data);
	
  if (cdata->done)
    return;

  std::string attached_body;
  unsigned int link1_attached_index = 0;
  unsigned int link2_attached_index = 0;

  if (cdata->selfCollisionTest) {
    dSpaceID s1 = dGeomGetSpace(o1);
    dSpaceID s2 = dGeomGetSpace(o2);
    if (s1 == s2 && s1 == cdata->selfSpace) {
      EnvironmentModelODE::kGeom* kg1 = reinterpret_cast<EnvironmentModelODE::kGeom*>(dGeomGetData(o1));
      EnvironmentModelODE::kGeom* kg2 = reinterpret_cast<EnvironmentModelODE::kGeom*>(dGeomGetData(o2));
      if (kg1 && kg2) {
	//two actual links
        if (kg1->geom[0] == o1 && kg2->geom[0] == o2) {
          if(kg2->allowedTouch[0][kg1->index] != kg1->allowedTouch[0][kg2->index]) {
            ROS_WARN_STREAM("Non-symmetric touch entries for " << kg2->link->getName() << " and " << kg1->link->getName());
	  }
	  if(kg2->allowedTouch[0][kg1->index] || kg1->allowedTouch[0][kg2->index]) {
            ROS_DEBUG_STREAM("Collision but allowed touch between " << kg2->link->getName() << " and " << kg1->link->getName());
	    return;
	  } else {
	    ROS_DEBUG_STREAM("Collision and no allowed touch between " << kg2->link->getName() << " and " << kg1->link->getName());
	  }
	} else {
	  // if we are looking at a link and an attached object
	  //if ((kg1->geom[0] == o1 && kg2->geom[0] != o2) || (kg1->geom[0] != o1 && kg2->geom[0] == o2)) {
          int p1 = -1, p2 = -1;
          for (unsigned int i = 0 ; i < kg1->geom.size() ; ++i) {
            if (kg1->geom[i] == o1) {
              p1 = i;
              break;
            }
          }
          for (unsigned int i = 0 ; i < kg2->geom.size() ; ++i) {
            if (kg2->geom[i] == o2) {
              p2 = i;
              break;
            }
          }
          assert(p1 >= 0 && p2 >= 0);
	  //two attached bodies collide
	  if(p1 > 0 && p2 > 0) {
	    //attached to the same link
	    if(kg1->geom[0] == kg2->geom[0]) {
	      return;
	    }
	    //TODO - deal with other cases
	  }
          if (p1 == 0) {
            if(kg2->geomAttachedBodyMap.find(o2) == kg2->geomAttachedBodyMap.end()) {
              ROS_WARN("No attached body for geom");
              return;
            } else {
              unsigned int bodyNum = kg2->geomAttachedBodyMap[o2];
              if (kg2->allowedTouch[bodyNum][kg1->index]) {
                ROS_DEBUG_STREAM("Collision but allowed touch between attached body of " << kg2->link->getName() << " and " << kg1->link->getName());
                ROS_DEBUG_STREAM("Attached body id is " << kg2->link->getAttachedBodyModels()[bodyNum-1]->getName());
                attached_body =  kg2->link->getAttachedBodyModels()[bodyNum-1]->getName();
                return;              
              } else {
                ROS_DEBUG_STREAM("Collision and no allowed touch between attached body of " << kg2->link->getName() << " and " << kg1->link->getName());
                ROS_DEBUG_STREAM("Attached body id is " << kg2->link->getAttachedBodyModels()[bodyNum-1]->getName());
                link2_attached_index = bodyNum;
              }
            }
          } else {
            ROS_DEBUG("P1 is not zero");
          }
          if (p2 == 0) {
            if(kg1->geomAttachedBodyMap.find(o1) == kg1->geomAttachedBodyMap.end()) {
              ROS_WARN("No attached body for geom");
              return;
            } else {
              unsigned int bodyNum = kg1->geomAttachedBodyMap[o1];
              if (kg1->allowedTouch[bodyNum][kg2->index]) {
                ROS_DEBUG_STREAM("Collision but allowed touch between attached body of " << kg1->link->getName() << " and " << kg2->link->getName());
                ROS_DEBUG_STREAM("Attached body id is " << kg1->link->getAttachedBodyModels()[bodyNum-1]->getName());
                return;
              } else {
                ROS_DEBUG_STREAM("Collision and no allowed touch between attached body of " << kg1->link->getName() << " and " << kg2->link->getName());
                ROS_DEBUG_STREAM("Attached body id is " << kg1->link->getAttachedBodyModels()[bodyNum-1]->getName());
                link1_attached_index = bodyNum;
              }
            }
          } else {
            ROS_DEBUG("P2 is not zero");
          }          
        }
      }
      //if we don't return we set these for the rest of the computation
      cdata->link1 = kg1->link;
      cdata->link2 = kg2->link;
    }
  } else {
    ROS_DEBUG("No self collision test");
  }

  unsigned int num_contacts = 1;
  if(cdata->contacts) {
    num_contacts = std::min(MAX_ODE_CONTACTS, cdata->max_contacts);
  }
  num_contacts = std::max(num_contacts, (unsigned int)1);
  
  dContactGeom contactGeoms[num_contacts];
  int numc = dCollide (o1, o2, num_contacts,
                       &(contactGeoms[0]), sizeof(dContactGeom));

  if(!cdata->contacts) {
    if (numc)
    {
      cdata->collides = true;
      cdata->done = true;
    }
  } else if (numc) {
    for (int i = 0 ; i < numc ; ++i)
    {
      if(cdata->max_contacts > 0 && cdata->contacts->size() >= cdata->max_contacts) {
        break;
      }
      ROS_DEBUG_STREAM("Contact at " << contactGeoms[i].pos[0] << " " 
                       << contactGeoms[i].pos[1] << " " << contactGeoms[i].pos[2]);
      
      btVector3 pos(contactGeoms[i].pos[0], contactGeoms[i].pos[1], contactGeoms[i].pos[2]);
      
      if (cdata->allowed)
      {
        dSpaceID s1 = dGeomGetSpace(o1);
        dSpaceID s2 = dGeomGetSpace(o2);
        
        bool b1 = s1 == cdata->selfSpace;
        bool b2 = s2 == cdata->selfSpace;
        
        if (b1 != b2)
        {
          std::string link_name;
          if (b1)
          {
            EnvironmentModelODE::kGeom* kg1 = reinterpret_cast<EnvironmentModelODE::kGeom*>(dGeomGetData(o1));
            link_name = kg1->link->getName();
          }
          else
          {
            EnvironmentModelODE::kGeom* kg2 = reinterpret_cast<EnvironmentModelODE::kGeom*>(dGeomGetData(o2));
            link_name = kg2->link->getName();
          }
          
          bool allow = false;
          for (unsigned int j = 0 ; !allow && j < cdata->allowed->size() ; ++j)
          {
            if (cdata->allowed->at(j).bound->containsPoint(pos) && cdata->allowed->at(j).depth > fabs(contactGeoms[i].depth))
            {
              for (unsigned int k = 0 ; k < cdata->allowed->at(j).links.size() ; ++k) {
                if (cdata->allowed->at(j).links[k] == link_name)
                {
                  allow = true;
                  break;
                }	
              }				
            }
          }
          
          if (allow)
            continue;
        }
      }
        
      cdata->collides = true;
      
      collision_space::EnvironmentModelODE::Contact add;
      
      add.pos = pos;
      
      add.normal.setX(contactGeoms[i].normal[0]);
      add.normal.setY(contactGeoms[i].normal[1]);
      add.normal.setZ(contactGeoms[i].normal[2]);
      
      add.depth = contactGeoms[i].depth;
      
      add.link1 = cdata->link1;
      add.link1_attached_body_index = link1_attached_index;
      add.link2 = cdata->link2;          
      add.link2_attached_body_index = link2_attached_index;
      
      cdata->contacts->push_back(add);
    }
    if (cdata->max_contacts > 0 && cdata->contacts->size() >= cdata->max_contacts)
      cdata->done = true;    
  }
}
}

bool collision_space::EnvironmentModelODE::getCollisionContacts(const std::vector<AllowedContact> &allowedContacts, std::vector<Contact> &contacts, unsigned int max_count) const
{
  contacts.clear();
  CollisionData cdata;
  if(use_set_collision_matrix_) {
    cdata.selfCollisionTest = &set_collision_matrix_;
  } else {
    cdata.selfCollisionTest = &m_selfCollisionTest;
  }
  cdata.contacts = &contacts;
  cdata.max_contacts = max_count;
  if (!allowedContacts.empty())
    cdata.allowed = &allowedContacts;
  cdata.selfSpace = m_modelGeom.space;
  contacts.clear();
  checkThreadInit();
  testCollision(&cdata);
  return cdata.collides;
}

bool collision_space::EnvironmentModelODE::isCollision(void) const
{
  CollisionData cdata;
  cdata.selfSpace = m_modelGeom.space;
  checkThreadInit();
  testCollision(&cdata);
  return cdata.collides;
}

bool collision_space::EnvironmentModelODE::isSelfCollision(void) const
{
  CollisionData cdata; 
  cdata.selfSpace = m_modelGeom.space;
  checkThreadInit();
  testSelfCollision(&cdata);
  return cdata.collides;
}

void collision_space::EnvironmentModelODE::testSelfCollision(CollisionData *cdata) const
{ 
  if(use_set_collision_matrix_) {
    cdata->selfCollisionTest = &set_collision_matrix_;
  } else {
    cdata->selfCollisionTest = &m_selfCollisionTest;
  }
  dSpaceCollide(m_modelGeom.space, cdata, nearCallbackFn);
}

void collision_space::EnvironmentModelODE::testBodyCollision(CollisionNamespace *cn, CollisionData *cdata) const
{ 
  if (cn->collide2.empty())
  {
    // if there is no collide2 structure, then there is a list of geoms
    for (int i = m_modelGeom.linkGeom.size() - 1 ; i >= 0 && !cdata->done ; --i)
    {
      kGeom *kg = m_modelGeom.linkGeom[i];

      /* skip disabled bodies */
      if (!kg->enabled)
        continue;
      const unsigned int ng = kg->geom.size();
      cdata->link1 = kg->link;
          
      for (unsigned int ig = 0 ; ig < ng && !cdata->done ; ++ig)
      {
        if(use_set_collision_matrix_) { 
          std::string kg1name = cn->name;
          std::string kg2name;
          if(ig == 0) {
            //this is the actual link
            kg2name = cdata->link1->getName();
          } else {
            //it's an attached body
            if(kg->geomAttachedBodyMap.find(kg->geom[ig]) == kg->geomAttachedBodyMap.end()) {
              ROS_WARN_STREAM("Attached body geom for link " << cdata->link1->getName() << " num " << ig << " not found in geomMap");              
              kg2name = cdata->link1->getName();  
            } else {
              kg2name = kg->link->getAttachedBodyModels()[kg->geomAttachedBodyMap[kg->geom[ig]]-1]->getName();
            }
          }
          if(set_collision_ind_.find(kg1name) == set_collision_ind_.end()) {
            ROS_ERROR_STREAM("1Problem in collision_space.  Using set collision but can't find link name for link one " << kg1name);
            return;
          }
          if(set_collision_ind_.find(kg2name) == set_collision_ind_.end()) {
            ROS_ERROR_STREAM("1Problem in collision_space.  Using set collision but can't find link or attached body name " << kg2name);
            return;
          }
          
          unsigned int kg1ind = set_collision_ind_.find(kg1name)->second;
          unsigned int kg2ind = set_collision_ind_.find(kg2name)->second;
          
          if(set_collision_matrix_[kg1ind][kg2ind]) {
            //ROS_DEBUG_STREAM("Not checking collisions between " << kg1name << " and " << kg2name);
            continue;
          } else {
            //ROS_DEBUG_STREAM("Checking collisions between " << kg1name << " and " << kg2name);
          }
        }


        dGeomID g1 = m_modelGeom.linkGeom[i]->geom[ig];
        dReal aabb1[6];
        dGeomGetAABB(g1, aabb1);
        
        for (int j = cn->geoms.size() - 1 ; j >= 0 ; --j)
        {
          dGeomID g2 = cn->geoms[j];
          dReal aabb2[6];
          dGeomGetAABB(g2, aabb2);
	
          bool currentCollides = cdata->collides;
          cdata->collides = false;
	    
          unsigned int current_contacts_size = 0;
          if(cdata->contacts) {
            current_contacts_size = cdata->contacts->size();
          }

          if (!(aabb1[2] > aabb2[3] ||
                aabb1[3] < aabb2[2] ||
                aabb1[4] > aabb2[5] ||
                aabb1[5] < aabb2[4]))
            dSpaceCollide2(g1, g2, cdata, nearCallbackFn);
		    
          if (cdata->collides) {
            kGeom *kg = m_modelGeom.linkGeom[i];
            if(m_verbose) {
              ROS_INFO("Collision between body in namespace %s and link %s",
                       cn->name.c_str(), m_modelGeom.linkGeom[i]->link->getName().c_str());
              if(ig > 0) {
                ROS_INFO_STREAM("Collision is really between namespace " << cn->name.c_str() << " and attached body "
                                << kg->link->getAttachedBodyModels()[kg->geomAttachedBodyMap[kg->geom[ig]]-1]->getName());
              }
            }
            if(cdata->contacts) {
              if(cdata->contacts->size() <= current_contacts_size) {
                ROS_WARN("Supposedly new contacts but none in vector");
              } else {
                for(unsigned int j = current_contacts_size; j < cdata->contacts->size(); j++) {
                  if(!cdata->contacts->at(j).object_name.empty()) {
                    ROS_WARN("Object name really should be empty");
                  } else if(cdata->contacts->at(j).link2 == NULL) {
                    cdata->contacts->at(j).object_name = cn->name;
                  } 
                  if(ig > 0) {
                    cdata->contacts->at(j).link1_attached_body_index = kg->geomAttachedBodyMap[kg->geom[ig]];
                  }
                }
              }
            }
          } else {
            //if this wasn't a collision, but we had previous collisions, this gets set to true
            cdata->collides = currentCollides;
          }
        }
      }
    }
  }
  else
  {
    cn->collide2.setup();
    for (int i = m_modelGeom.linkGeom.size() - 1 ; i >= 0 && !cdata->done ; --i) {
      if (m_modelGeom.linkGeom[i]->enabled)
      {
        kGeom *kg = m_modelGeom.linkGeom[i];
        const unsigned int ng = m_modelGeom.linkGeom[i]->geom.size();
        cdata->link1 = m_modelGeom.linkGeom[i]->link;
        for (unsigned int ig = 0 ; ig < ng && !cdata->done ; ++ig)
        {
          
          if(use_set_collision_matrix_) { 
            std::string kg1name = cn->name;
            std::string kg2name;
            if(ig == 0) {
              //this is the actual link
              kg2name = cdata->link1->getName();
            } else {
              //it's an attached body
              if(kg->geomAttachedBodyMap.find(kg->geom[ig]) == kg->geomAttachedBodyMap.end()) {
                ROS_WARN_STREAM("Attached body geom for link " << cdata->link1->getName() << " num " << ig << " not found in geomMap");              
                kg2name = cdata->link1->getName();  
              } else {
                kg2name = kg->link->getAttachedBodyModels()[kg->geomAttachedBodyMap[kg->geom[ig]]-1]->getName();
              }
            }
            if(set_collision_ind_.find(kg1name) == set_collision_ind_.end()) {
              ROS_ERROR_STREAM("2Problem in collision_space.  Using set collision but can't find link name " << kg1name);
              return;
            }
            if(set_collision_ind_.find(kg2name) == set_collision_ind_.end()) {
              ROS_ERROR_STREAM("2Problem in collision_space.  Using set collision but can't find link or attached body name " << kg2name);
              return;
            }
            
            unsigned int kg1ind = set_collision_ind_.find(kg1name)->second;
            unsigned int kg2ind = set_collision_ind_.find(kg2name)->second;
            
            if(set_collision_matrix_[kg1ind][kg2ind]) {
              //ROS_DEBUG_STREAM("Not checking collisions between " << kg1name << " and " << kg2name);
              continue;
            } else {
              //ROS_DEBUG_STREAM("Checking collisions between " << kg1name << " and " << kg2name);
            }
          }
          bool currentCollides = cdata->collides;
          cdata->collides = false;

          //have to figure
          unsigned int current_contacts_size = 0;
          if(cdata->contacts) {
            current_contacts_size = cdata->contacts->size();
          }

          cn->collide2.collide(m_modelGeom.linkGeom[i]->geom[ig], cdata, nearCallbackFn);
          //fresh collision
          if (cdata->collides) {
            kGeom *kg = m_modelGeom.linkGeom[i];
            if(m_verbose) {
              ROS_INFO("Collision between body in namespace %s and link %s",
                       cn->name.c_str(), m_modelGeom.linkGeom[i]->link->getName().c_str());
              if(ig > 0) {
                ROS_INFO_STREAM("Collision is really between namespace " << cn->name.c_str() << " and attached body "
                                << kg->link->getAttachedBodyModels()[kg->geomAttachedBodyMap[kg->geom[ig]]-1]->getName());
              }
            }
            if(cdata->contacts) {
              if(cdata->contacts->size() <= current_contacts_size) {
                ROS_WARN("Supposedly new contacts but none in vector");
              } else {
                for(unsigned int j = current_contacts_size; j < cdata->contacts->size(); j++) {
                  if(!cdata->contacts->at(j).object_name.empty()) {
                    ROS_WARN("Object name really should be empty");
                  } else if(cdata->contacts->at(j).link2 == NULL) {
                    cdata->contacts->at(j).object_name = cn->name;
                  } 
                  if(ig > 0) {
                    cdata->contacts->at(j).link1_attached_body_index = kg->geomAttachedBodyMap[kg->geom[ig]];
                  }
                }
              }
            }
          } else {
            //if this wasn't a collision, but we had previous collisions, this gets set to true
            cdata->collides = currentCollides;
          }
        }
      }
    }
  }
}

void collision_space::EnvironmentModelODE::testCollision(CollisionData *cdata) const
{
  /* check self collision */
  if (m_selfCollision)
    testSelfCollision(cdata);
    
  if (!cdata->done)
  {
    cdata->link2 = NULL;
    /* check collision with other ode bodies */
    for (std::map<std::string, CollisionNamespace*>::const_iterator it = m_collNs.begin() ; it != m_collNs.end() && !cdata->done ; ++it) {
      testBodyCollision(it->second, cdata);
    }
    cdata->done = true;
  }
}

void collision_space::EnvironmentModelODE::addObjects(const std::string &ns, const std::vector<shapes::Shape*> &shapes, const std::vector<btTransform> &poses)
{
  assert(shapes.size() == poses.size());
  std::map<std::string, CollisionNamespace*>::iterator it = m_collNs.find(ns);
  CollisionNamespace* cn = NULL;    
  if (it == m_collNs.end())
  {
    cn = new CollisionNamespace(ns);
     m_collNs[ns] = cn;
  }
  else {
     cn = it->second;
  }

  //we're going to create the namespace in m_objects even if it doesn't have anything in it
  m_objects->addObjectNamespace(ns);

  unsigned int n = shapes.size();
  for (unsigned int i = 0 ; i < n ; ++i)
  {
    dGeomID g = createODEGeom(cn->space, cn->storage, shapes[i], 1.0, 0.0);
    assert(g);
    dGeomSetData(g, reinterpret_cast<void*>(shapes[i]));
    updateGeom(g, poses[i]);
    cn->collide2.registerGeom(g);
    m_objects->addObject(ns, shapes[i], poses[i]);
  }
  cn->collide2.setup();
}

void collision_space::EnvironmentModelODE::addObject(const std::string &ns, shapes::Shape *shape, const btTransform &pose)
{
  std::map<std::string, CollisionNamespace*>::iterator it = m_collNs.find(ns);
  CollisionNamespace* cn = NULL;    
  if (it == m_collNs.end())
  {
    cn = new CollisionNamespace(ns);
    m_collNs[ns] = cn;
  }
  else
    cn = it->second;
    
  dGeomID g = createODEGeom(cn->space, cn->storage, shape, 1.0, 0.0);
  assert(g);
  dGeomSetData(g, reinterpret_cast<void*>(shape));

  updateGeom(g, pose);
  cn->geoms.push_back(g);
  m_objects->addObject(ns, shape, pose);
}

void collision_space::EnvironmentModelODE::addObject(const std::string &ns, shapes::StaticShape* shape)
{   
  std::map<std::string, CollisionNamespace*>::iterator it = m_collNs.find(ns);
  CollisionNamespace* cn = NULL;    
  if (it == m_collNs.end())
  {
    cn = new CollisionNamespace(ns);
    m_collNs[ns] = cn;
  }
  else
    cn = it->second;

  dGeomID g = createODEGeom(cn->space, cn->storage, shape);
  assert(g);
  dGeomSetData(g, reinterpret_cast<void*>(shape));
  cn->geoms.push_back(g);
  m_objects->addObject(ns, shape);
}

void collision_space::EnvironmentModelODE::clearObjects(void)
{
  for (std::map<std::string, CollisionNamespace*>::iterator it = m_collNs.begin() ; it != m_collNs.end() ; ++it)
    delete it->second;
  m_collNs.clear();
  m_objects->clearObjects();
}

void collision_space::EnvironmentModelODE::clearObjects(const std::string &ns)
{
  std::map<std::string, CollisionNamespace*>::iterator it = m_collNs.find(ns);
  if (it != m_collNs.end()) {
    delete it->second;
    m_collNs.erase(ns);
  }
  m_objects->clearObjects(ns);
}

void collision_space::EnvironmentModelODE::updateAllowedTouch()
{
  if(use_set_collision_matrix_) {
    ROS_WARN("Shouldn't update allowed touch in use collision matrix mode");
    return;
  }

  const unsigned int n = m_modelGeom.linkGeom.size();    
  for (unsigned int i = 0 ; i < n ; ++i)
  {
    kGeom *kg = m_modelGeom.linkGeom[i];
    kg->allowedTouch.resize(kg->geom.size());
	
    if (kg->geom.empty())
      continue;
	
    kg->allowedTouch[0].resize(n);
	
    // compute the allowed touch for robot links
    for (unsigned int j = 0 ; j < n ; ++j)
      // if self collision checking with link j is disabled, we are allowed to touch link i with geom 0
      // otherwise, we are not
      kg->allowedTouch[0][j] = m_selfCollisionTest[kg->index][j];

    const unsigned int nab = kg->link->getAttachedBodyModels().size();
    for (unsigned int k = 0 ; k < nab ; ++k)
    {
      kg->allowedTouch[k + 1].clear();
      kg->allowedTouch[k + 1].resize(n, false);
      //allowed to touch attached link by default
      //kg->link->attached_bodies[k]->touch_links.push_back(kg->link->getName());
      //TODO - make sure this goes in collision_space_monitor
      for (unsigned int j = 0 ; j < kg->link->getAttachedBodyModels()[k]->getTouchLinks().size() ; ++j)
      {
        const std::string &tlink = kg->link->getAttachedBodyModels()[k]->getTouchLinks()[j];
        std::map<std::string, unsigned int>::const_iterator it = m_collisionLinkIndex.find(tlink);
        if (it != m_collisionLinkIndex.end())
          kg->allowedTouch[k + 1][it->second] = true;
        else
          ROS_WARN("Unknown link %s specified for touch link", tlink.c_str());
      }
    }
  }
}

void collision_space::EnvironmentModelODE::getDefaultAllowedCollisionMatrix(std::vector<std::vector<bool> > &curAllowed,
                                                                            std::map<std::string, unsigned int> &vecIndices) const{

  //shouldn't need to call updateAllowedTouch
  
  curAllowed.clear();
  vecIndices.clear();

  vecIndices = m_collisionLinkIndex;

  unsigned int num_links = m_modelGeom.linkGeom.size();    

  unsigned int num_attached = 0;

  for (unsigned int i = 0 ; i < num_links ; ++i)
  {
    kGeom *kg = m_modelGeom.linkGeom[i];
    num_attached += kg->link->getAttachedBodyModels().size();
  }

  std::vector<std::string> ns = m_objects->getNamespaces();
  unsigned int num_objects = ns.size();
  //for now each namespace is considered one object

  unsigned int total_num = num_links+num_attached+num_objects;

  //all values to true to false to start, meaning no allowed collisions
  std::vector<bool> all_false(total_num, false);
  curAllowed.resize(total_num, all_false);

  //this sets the matrix for links, but not for objects or attached bodies, essentially only filling out part of the matrix
  for(unsigned int i = 0; i < num_links; i++) {
    kGeom *kg = m_modelGeom.linkGeom[i];
    for(unsigned int j = 0; j < num_links; j++) {
      curAllowed[i][j] = kg->allowedTouch[0][j];
    }
  }
  //this sets indices for objects
  unsigned int cur_index_counter = num_links;
  for (unsigned int i = 0 ; i < ns.size() ; ++i)
  {
    vecIndices[ns[i]] = cur_index_counter++;
  }

  //now we have to deal with attached bodies
  for(unsigned int i = 0; i < num_links; i++) {
    kGeom *kg = m_modelGeom.linkGeom[i];
    for (unsigned int j = 0 ; j < kg->link->getAttachedBodyModels().size(); ++j) {
      vecIndices[kg->link->getAttachedBodyModels()[j]->getName()] = cur_index_counter;
      //ROS_DEBUG_STREAM("Setting attached body id " << kg->link->attached_bodies[j]->id << " to " << cur_index_counter);
      for(unsigned int k = 0; k < kg->link->getAttachedBodyModels()[j]->getTouchLinks().size(); k++) {
        std::string lname = kg->link->getAttachedBodyModels()[j]->getTouchLinks()[k];
        std::map<std::string, unsigned int>::const_iterator it = vecIndices.find(lname);
        if (it != vecIndices.end()) {
          curAllowed[vecIndices[lname]][cur_index_counter] = true;
          curAllowed[cur_index_counter][vecIndices[lname]] = true;
        }
      }
      cur_index_counter++;
    }
  }
}

void collision_space::EnvironmentModelODE::setAllowedCollisionMatrix(const std::vector<std::vector<bool> > &matrix,
                                                                     const std::map<std::string, unsigned int > &ind) {

  EnvironmentModel::setAllowedCollisionMatrix(matrix, ind);

  //we also need to set our allowed touch matrices
  const unsigned int n = m_modelGeom.linkGeom.size();    
  for (unsigned int i = 0 ; i < n ; ++i)
  {
    kGeom *kg = m_modelGeom.linkGeom[i];
    kg->allowedTouch.resize(kg->geom.size());
    kg->allowedTouch[0].resize(n);

    if(ind.find(kg->link->getName()) == ind.end()) {
      ROS_WARN_STREAM("Can't find link name " << kg->link->getName() << " in collision matrix ind.");
      continue;
    }

    unsigned int cur_mat_ind = ind.find(kg->link->getName())->second;
    if(kg->index != cur_mat_ind) {
      ROS_ERROR_STREAM("Index for link " << kg->link->getName() << " has changed.  Gonna be trouble in collision test.");
    }
    
    //setting 0 to our allowed touch
    for (unsigned int j = 0 ; j < n ; ++j) {
      kg->allowedTouch[0][j] = matrix[cur_mat_ind][j];
    }

    const unsigned int nab = kg->link->getAttachedBodyModels().size();
    for (unsigned int j = 0 ; j < nab ; ++j)
    {
      kg->allowedTouch[j + 1].clear();
      kg->allowedTouch[j + 1].resize(n, false);
      std::string aname = kg->link->getAttachedBodyModels()[j]->getName();
      if(ind.find(aname) == ind.end()) {
        ROS_WARN_STREAM("Attached body id " << aname << " does not seem to have an entry in the matrix");
        continue;
      }
      unsigned int a_ind = ind.find(aname)->second;
      for(unsigned int k = 0; k < n; k++) {
        kg->allowedTouch[j + 1][k] = matrix[a_ind][k];
      }
      //don't need symmetry, as the test is one way in nearcallbackFn
    }
  }
}

void collision_space::EnvironmentModelODE::revertAllowedCollisionMatrix() {
  EnvironmentModel::revertAllowedCollisionMatrix();
  updateAllowedTouch();
}

void collision_space::EnvironmentModelODE::addSelfCollisionGroup(const std::vector<std::string> &group1,
                                                                 const std::vector<std::string> &group2)
{
  EnvironmentModel::addSelfCollisionGroup(group1,group2);
  updateAllowedTouch();
}

void collision_space::EnvironmentModelODE::removeSelfCollisionGroup(const std::vector<std::string> &group1,
                                                                    const std::vector<std::string> &group2)
{
  EnvironmentModel::removeSelfCollisionGroup(group1,group2);
  updateAllowedTouch();
}

void collision_space::EnvironmentModelODE::removeCollidingObjects(const shapes::StaticShape *shape)
{
  checkThreadInit();
  dSpaceID space = dSimpleSpaceCreate(0);
  ODEStorage storage;
  dGeomID g = createODEGeom(space, storage, shape);
  removeCollidingObjects(g);
  dSpaceDestroy(space);
}

void collision_space::EnvironmentModelODE::removeCollidingObjects(const shapes::Shape *shape, const btTransform &pose)
{   
  checkThreadInit();
  dSpaceID space = dSimpleSpaceCreate(0);
  ODEStorage storage;
  dGeomID g = createODEGeom(space, storage, shape, 1.0, 0.0);
  updateGeom(g, pose);
  removeCollidingObjects(g);
  dSpaceDestroy(space);
}

void collision_space::EnvironmentModelODE::removeCollidingObjects(dGeomID geom)
{
  CollisionData cdata;
  for (std::map<std::string, CollisionNamespace*>::iterator it = m_collNs.begin() ; it != m_collNs.end() ; ++it)
  {
    std::map<void*, bool> remove;
	
    // update the set of geoms
    unsigned int n = it->second->geoms.size();
    std::vector<dGeomID> replaceGeoms;
    replaceGeoms.reserve(n);
    for (unsigned int j = 0 ; j < n ; ++j)
    {
      cdata.done = cdata.collides = false;
      dSpaceCollide2(geom, it->second->geoms[j], &cdata, nearCallbackFn);
      if (cdata.collides)
      {
        remove[dGeomGetData(it->second->geoms[j])] = true;
        dGeomDestroy(it->second->geoms[j]);
      }
      else
      {
        replaceGeoms.push_back(it->second->geoms[j]);
        remove[dGeomGetData(it->second->geoms[j])] = false;
      }
    }
    it->second->geoms = replaceGeoms;
	
    // update the collide2 structure
    std::vector<dGeomID> geoms;
    it->second->collide2.getGeoms(geoms);
    n = geoms.size();
    for (unsigned int j = 0 ; j < n ; ++j)
    {
      cdata.done = cdata.collides = false;
      dSpaceCollide2(geom, geoms[j], &cdata, nearCallbackFn);
      if (cdata.collides)
      {
        remove[dGeomGetData(geoms[j])] = true;
        it->second->collide2.unregisterGeom(geoms[j]);
        dGeomDestroy(geoms[j]);
      }
      else
        remove[dGeomGetData(geoms[j])] = false;
    }
	
    EnvironmentObjects::NamespaceObjects &no = m_objects->getObjects(it->first);
	
    std::vector<shapes::Shape*> replaceShapes;
    std::vector<btTransform>    replaceShapePoses;
    n = no.shape.size();
    replaceShapes.reserve(n);
    replaceShapePoses.reserve(n);
    for (unsigned int i = 0 ; i < n ; ++i)
      if (remove[reinterpret_cast<void*>(no.shape[i])])
        delete no.shape[i];
      else
      {
        replaceShapes.push_back(no.shape[i]);
        replaceShapePoses.push_back(no.shapePose[i]);
      }
    no.shape = replaceShapes;
    no.shapePose = replaceShapePoses;
	
    std::vector<shapes::StaticShape*> replaceStaticShapes;
    n = no.staticShape.size();
    replaceStaticShapes.resize(n);
    for (unsigned int i = 0 ; i < n ; ++i)
      if (remove[reinterpret_cast<void*>(no.staticShape[i])])
        delete no.staticShape[i];
      else
        replaceStaticShapes.push_back(no.staticShape[i]);
    no.staticShape = replaceStaticShapes;
  }
}

int collision_space::EnvironmentModelODE::setCollisionCheck(const std::string &link, bool state)
{ 
  int result = -1;
  for (unsigned int j = 0 ; j < m_modelGeom.linkGeom.size() ; ++j)
  {
    if (m_modelGeom.linkGeom[j]->link->getName() == link)
    {
      result = m_modelGeom.linkGeom[j]->enabled ? 1 : 0;
      m_modelGeom.linkGeom[j]->enabled = state;
      break;
    }
  }

  return result;    
}

void collision_space::EnvironmentModelODE::setCollisionCheckLinks(const std::vector<std::string> &links, bool state)
{ 
  if(links.empty())
    return;

  for (unsigned int i = 0 ; i < m_modelGeom.linkGeom.size() ; ++i)
  {
    for (unsigned int j=0; j < links.size(); j++)
    {
      if (m_modelGeom.linkGeom[i]->link->getName() == links[j])
      {
        m_modelGeom.linkGeom[i]->enabled = state;
        break;
      }
    }
  }
}

void collision_space::EnvironmentModelODE::setCollisionCheckOnlyLinks(const std::vector<std::string> &links, bool state)
{ 
  if(links.empty())
    return;

  for (unsigned int i = 0 ; i < m_modelGeom.linkGeom.size() ; ++i)
  {
    int result = -1;
    for (unsigned int j=0; j < links.size(); j++)
    {
      if (m_modelGeom.linkGeom[i]->link->getName() == links[j])
      {
        m_modelGeom.linkGeom[i]->enabled = state;
        result = j;
        break;
      }        
    }
    if(result < 0)
      m_modelGeom.linkGeom[i]->enabled = !state;      
  }
}

void collision_space::EnvironmentModelODE::setCollisionCheckAll(bool state)
{ 
  for (unsigned int j = 0 ; j < m_modelGeom.linkGeom.size() ; ++j)
  {
    m_modelGeom.linkGeom[j]->enabled = state;
  }
}

dGeomID collision_space::EnvironmentModelODE::copyGeom(dSpaceID space, ODEStorage &storage, dGeomID geom, ODEStorage &sourceStorage) const
{
  int c = dGeomGetClass(geom);
  dGeomID ng = NULL;
  bool location = true;
  switch (c)
  {
  case dSphereClass:
    ng = dCreateSphere(space, dGeomSphereGetRadius(geom));
    break;
  case dBoxClass:
    {
      dVector3 r;
      dGeomBoxGetLengths(geom, r);
      ng = dCreateBox(space, r[0], r[1], r[2]);
    }
    break;
  case dCylinderClass:
    {
      dReal r, l;
      dGeomCylinderGetParams(geom, &r, &l);
      ng = dCreateCylinder(space, r, l);
    }
    break;
  case dPlaneClass:
    {
      dVector4 p;
      dGeomPlaneGetParams(geom, p);
      ng = dCreatePlane(space, p[0], p[1], p[2], p[3]);
      location = false;
    }
    break;
  case dTriMeshClass:
    {
      dTriMeshDataID tdata = dGeomTriMeshGetData(geom);
      dTriMeshDataID cdata = dGeomTriMeshDataCreate();
      for (unsigned int i = 0 ; i < sourceStorage.mesh.size() ; ++i)
        if (sourceStorage.mesh[i].data == tdata)
        {
          unsigned int p = storage.mesh.size();
          storage.mesh.resize(p + 1);
          storage.mesh[p].nVertices = sourceStorage.mesh[i].nVertices;
          storage.mesh[p].nIndices = sourceStorage.mesh[i].nIndices;
          storage.mesh[p].indices = new dTriIndex[storage.mesh[p].nIndices];
          for (int j = 0 ; j < storage.mesh[p].nIndices ; ++j)
            storage.mesh[p].indices[j] = sourceStorage.mesh[i].indices[j];
          storage.mesh[p].vertices = new double[storage.mesh[p].nVertices];
          for (int j = 0 ; j < storage.mesh[p].nVertices ; ++j)
            storage.mesh[p].vertices[j] = sourceStorage.mesh[i].vertices[j];
          dGeomTriMeshDataBuildDouble(cdata, storage.mesh[p].vertices, sizeof(double) * 3, storage.mesh[p].nVertices, storage.mesh[p].indices, storage.mesh[p].nIndices, sizeof(dTriIndex) * 3);
          storage.mesh[p].data = cdata;
          break;
        }
      ng = dCreateTriMesh(space, cdata, NULL, NULL, NULL);
    }
    break;
  default:
    assert(0); // this should never happen
    break;
  }
    
  if (ng && location)
  {
    const dReal *pos = dGeomGetPosition(geom);
    dGeomSetPosition(ng, pos[0], pos[1], pos[2]);
    dQuaternion q;
    dGeomGetQuaternion(geom, q);
    dGeomSetQuaternion(ng, q);
  }
    
  return ng;
}

collision_space::EnvironmentModel* collision_space::EnvironmentModelODE::clone(void) const
{
  EnvironmentModelODE *env = new EnvironmentModelODE();
  env->m_collisionLinks = m_collisionLinks;
  env->m_collisionLinkIndex = m_collisionLinkIndex;
  env->m_selfCollisionTest = m_selfCollisionTest;	
  env->m_selfCollision = m_selfCollision;
  env->m_verbose = m_verbose;
  env->m_robotScale = m_robotScale;
  env->m_robotPadd = m_robotPadd;
  env->m_robotModel = new planning_models::KinematicModel(*m_robotModel);
  env->createODERobotModel();
  for (unsigned int j = 0 ; j < m_modelGeom.linkGeom.size() ; ++j)
    env->m_modelGeom.linkGeom[j]->enabled = m_modelGeom.linkGeom[j]->enabled;

  for (std::map<std::string, CollisionNamespace*>::const_iterator it = m_collNs.begin() ; it != m_collNs.end() ; ++it)
  {
    // construct a map of the shape pointers we have; this points to the index positions where they are stored;
    std::map<void*, int> shapePtrs;
    const EnvironmentObjects::NamespaceObjects &ns = m_objects->getObjects(it->first);
    unsigned int n = ns.staticShape.size();
    for (unsigned int i = 0 ; i < n ; ++i)
      shapePtrs[ns.staticShape[i]] = -1 - i;
    n = ns.shape.size();
    for (unsigned int i = 0 ; i < n ; ++i)
      shapePtrs[ns.shape[i]] = i;
    
    // copy the collision namespace structure, geom by geom
    CollisionNamespace *cn = new CollisionNamespace(it->first);
    env->m_collNs[it->first] = cn;
    n = it->second->geoms.size();
    cn->geoms.reserve(n);
    for (unsigned int i = 0 ; i < n ; ++i)
    {
      dGeomID newGeom = copyGeom(cn->space, cn->storage, it->second->geoms[i], it->second->storage);
      int idx = shapePtrs[dGeomGetData(it->second->geoms[i])];
      if (idx < 0) // static geom
      {
        shapes::StaticShape *newShape = shapes::cloneShape(ns.staticShape[-idx - 1]);
        dGeomSetData(newGeom, reinterpret_cast<void*>(newShape));
        env->m_objects->addObject(it->first, newShape);
      }
      else // movable geom
      {
        shapes::Shape *newShape = shapes::cloneShape(ns.shape[idx]);
        dGeomSetData(newGeom, reinterpret_cast<void*>(newShape));
        env->m_objects->addObject(it->first, newShape, ns.shapePose[idx]);
      }
      cn->geoms.push_back(newGeom);
    }
    std::vector<dGeomID> geoms;
    it->second->collide2.getGeoms(geoms);
    n = geoms.size();
    for (unsigned int i = 0 ; i < n ; ++i)
    {
      dGeomID newGeom = copyGeom(cn->space, cn->storage, geoms[i], it->second->storage);
      int idx = shapePtrs[dGeomGetData(geoms[i])];
      if (idx < 0) // static geom
      {
        shapes::StaticShape *newShape = shapes::cloneShape(ns.staticShape[-idx - 1]);
        dGeomSetData(newGeom, reinterpret_cast<void*>(newShape));
        env->m_objects->addObject(it->first, newShape);
      }
      else // movable geom
      {
        shapes::Shape *newShape = shapes::cloneShape(ns.shape[idx]);
        dGeomSetData(newGeom, reinterpret_cast<void*>(newShape));
        env->m_objects->addObject(it->first, newShape, ns.shapePose[idx]);
      }
      cn->collide2.registerGeom(newGeom);
    }
  }
    
  return env;    
}
