/********************************************************************************
* @file       osgearthviewwidget.cpp
* @author     The OpenPilot Team Copyright (C) 2012.
* @addtogroup GCSPlugins GCS Plugins
* @{
* @addtogroup OsgEarthview Plugin Widget
* @{
* @brief Osg Earth view of UAV
*****************************************************************************/
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include "osgearthviewwidget.h"
#include <utils/stylehelper.h>
#include <iostream>
#include <QDebug>
#include <QPainter>
#include <QtOpenGL/QGLWidget>
#include <cmath>
#include <QtGui/QApplication>
#include <QLabel>
#include <QDebug>

#include <QtCore/QTimer>
#include <QtGui/QApplication>
#include <QtGui/QGridLayout>


#include <osg/Notify>
#include <osg/PositionAttitudeTransform>

#include <osgUtil/Optimizer>
#include <osgGA/StateSetManipulator>
#include <osgGA/GUIEventHandler>
#include <osgViewer/Viewer>
#include <osgViewer/ViewerEventHandlers>

#include <osgEarth/MapNode>
#include <osgEarth/XmlUtils>
#include <osgEarth/Viewpoint>

#include <osgEarthSymbology/Color>

#include <osgEarthAnnotation/AnnotationRegistry>
#include <osgEarthAnnotation/AnnotationData>
#include <osgEarthAnnotation/Decluttering>

#include <osgEarthDrivers/kml/KML>
#include <osgEarthDrivers/ocean_surface/OceanSurface>
#include <osgEarthDrivers/cache_filesystem/FileSystemCache>

#include <osgEarthUtil/EarthManipulator>
#include <osgEarthUtil/AutoClipPlaneHandler>
#include <osgEarthUtil/Controls>
#include <osgEarthUtil/SkyNode>
#include <osgEarthUtil/LatLongFormatter>
#include <osgEarthUtil/MouseCoordsTool>
#include <osgEarthUtil/ObjectLocator>

using namespace osgEarth::Util;
using namespace osgEarth::Util::Controls;
using namespace osgEarth::Symbology;
using namespace osgEarth::Drivers;
using namespace osgEarth::Annotation;

#include <osgViewer/CompositeViewer>
#include <osgViewer/ViewerEventHandlers>

#include <osgGA/TrackballManipulator>

#include <osgDB/ReadFile>

#include <osgQt/GraphicsWindowQt>

#include <iostream>

#include "utils/stylehelper.h"
#include "utils/homelocationutil.h"
#include "utils/worldmagmodel.h"
#include "utils/coordinateconversions.h"
#include "attitudeactual.h"
#include "homelocation.h"
#include "positionactual.h"

using namespace Utils;

OsgEarthviewWidget::OsgEarthviewWidget(QWidget *parent) : QWidget(parent)
{
    setThreadingModel(osgViewer::ViewerBase::CullThreadPerCameraDrawThreadPerContext);

    //setThreadingModel(osgViewer::ViewerBase::SingleThreaded);
    setAttribute(Qt::WA_PaintOnScreen, true);
    setAttribute(Qt::WA_NoSystemBackground, true);

    osg::Group* root = new osg::Group;
    osg::Node* earth = osgDB::readNodeFile("/Users/jcotton81/Documents/Programming/osgearth/tests/boston.earth");
    osgEarth::MapNode * mapNode = osgEarth::MapNode::findMapNode( earth );
    if (!mapNode)
    {
        qDebug() <<"Uhoh";
    }

    root->addChild(earth);

    osg::Node* airplane = createAirplane();
    uavPos = new osgEarth::Util::ObjectLocatorNode(mapNode->getMap());
    uavPos->getLocator()->setPosition( osg::Vec3d(-71.0763, 42.34425, 150) );
    uavPos->addChild(airplane);

    root->addChild(uavPos);

    osgUtil::Optimizer optimizer;
    optimizer.optimize(root);

    QWidget* popupWidget = createViewWidget( createCamera(0,0,600,600,"Earth",true), root);
    popupWidget->show();
    setLayout(new QVBoxLayout());
    //layout()->addWidget(popupWidget);

    connect( &_timer, SIGNAL(timeout()), this, SLOT(update()) );
    _timer.start( 10 );
}

OsgEarthviewWidget::~OsgEarthviewWidget()
{
}

QWidget* OsgEarthviewWidget::createViewWidget( osg::Camera* camera, osg::Node* scene )
{
    osgViewer::View* view = new osgViewer::View;
    view->setCamera( camera );

    addView( view );

    view->setSceneData( scene );
    view->addEventHandler( new osgViewer::StatsHandler );
    view->getDatabasePager()->setDoPreCompile( true );

    manip = new EarthManipulator();
    view->setCameraManipulator( manip );

    Grid* grid = new Grid();
    grid->setControl(0,0,new LabelControl("OpenPilot"));
    ControlCanvas::get(view, true)->addControl(grid);

    // zoom to a good startup position
    manip->setViewpoint( Viewpoint(-71.0763, 42.34425, 0, 24.261, -21.6, 650.0), 5.0 );
    //manip->setHomeViewpoint(Viewpoint("Boston", osg::Vec3d(-71.0763, 42.34425, 0), 24.261, -21.6, 3450.0));

    osgQt::GraphicsWindowQt* gw = dynamic_cast<osgQt::GraphicsWindowQt*>( camera->getGraphicsContext() );
    return gw ? gw->getGLWidget() : NULL;
}

osg::Camera* OsgEarthviewWidget::createCamera( int x, int y, int w, int h, const std::string& name="", bool windowDecoration=false )
{
    osg::DisplaySettings* ds = osg::DisplaySettings::instance().get();
    osg::ref_ptr<osg::GraphicsContext::Traits> traits = new osg::GraphicsContext::Traits;
    traits->windowName = name;
    traits->windowDecoration = windowDecoration;
    traits->x = x;
    traits->y = y;
    traits->width = w;
    traits->height = h;
    traits->doubleBuffer = true;
    traits->alpha = ds->getMinimumNumAlphaBits();
    traits->stencil = ds->getMinimumNumStencilBits();
    traits->sampleBuffers = ds->getMultiSamples();
    traits->samples = ds->getNumMultiSamples();

    osg::ref_ptr<osg::Camera> camera = new osg::Camera;
    camera->setGraphicsContext( new osgQt::GraphicsWindowQt(traits.get()) );

    camera->setClearColor( osg::Vec4(0.2, 0.2, 0.6, 1.0) );
    camera->setViewport( new osg::Viewport(0, 0, traits->width, traits->height) );
    camera->setProjectionMatrixAsPerspective(
                30.0f, static_cast<double>(traits->width)/static_cast<double>(traits->height), 1.0f, 10000.0f );
    return camera.release();
}

osg::Node* OsgEarthviewWidget::createAirplane()
{
    osg::Group* model = new osg::Group;
    osg::Node *cessna = osgDB::readNodeFile("/Users/jcotton81/Documents/Programming/OpenPilot/artwork/3D Model/multi/joes_cnc/J14-QT_+.3DS");
    if(cessna) {
        uavAttitudeAndScale = new osg::MatrixTransform();
        uavAttitudeAndScale->setMatrix(osg::Matrixd::scale(0.2e0,0.2e0,0.2e0));
        uavAttitudeAndScale->addChild( cessna );

        model->addChild(uavAttitudeAndScale);
    } else
        qDebug() << "Bad model file";
    return model;
}

void OsgEarthviewWidget::paintEvent( QPaintEvent* event )
{
    ExtensionSystem::PluginManager *pm = ExtensionSystem::PluginManager::instance();
    UAVObjectManager * objMngr = pm->getObject<UAVObjectManager>();

    PositionActual *positionActualObj = PositionActual::GetInstance(objMngr);
    PositionActual::DataFields positionActual = positionActualObj->getData();

    double LLA[3];
    double NED[3] = {positionActual.North, positionActual.East, positionActual.Down};
    double homeLLA[3] = {-71.0763, 42.34425, 50};

    CoordinateConversions().GetLLA(homeLLA, NED, LLA);
    uavPos->getLocator()->setPosition( osg::Vec3d(LLA[0], LLA[1], LLA[2]) );

    AttitudeActual *attitudeActualObj = AttitudeActual::GetInstance(objMngr);
    AttitudeActual::DataFields attitudeActual = attitudeActualObj->getData();

    osg::Quat quat(-attitudeActual.q2, -attitudeActual.q3, -attitudeActual.q4,attitudeActual.q1);
    uavAttitudeAndScale->setMatrix(osg::Matrixd::scale(0.05e0,0.05e0,0.05e0) * osg::Matrixd::rotate(quat));

    frame();
}

void OsgEarthviewWidget::resizeEvent(QResizeEvent *event)
{
}

