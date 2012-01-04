//    copyright: 'Cedric Pinson cedric.pinson@plopbyte.net'
#include <osg/Image>
#include <osg/Notify>
#include <osg/Geode>
#include <osg/GL>
#include <osg/Version>
#include <osg/Endian>
#include <osg/Projection>
#include <osg/MatrixTransform>
#include <osg/PositionAttitudeTransform>

#include <osgUtil/UpdateVisitor>
#include <osgDB/WriteFile>

#include <osgDB/Registry>
#include <osgDB/FileUtils>
#include <osgDB/FileNameUtils>

#include <osgAnimation/UpdateMatrixTransform>
#include <osgAnimation/AnimationManagerBase>
#include <osgAnimation/BasicAnimationManager>

#include <sstream>
#include <cassert>
#include <map>
#include "TangentSpace"
#include "JSON_Objects"

#include "GeometryOperation"
#include "Animation"

#define WRITER_VERSION 2


using namespace osg;

struct WriteVisitor : public osg::NodeVisitor
{
    typedef std::vector<osg::ref_ptr<osg::StateSet> > StateSetStack;

    std::map<osg::Object*, osg::ref_ptr<JSONObjectBase> > _maps;
    std::vector<osg::ref_ptr<JSONObject> > _parents;
    osg::ref_ptr<JSONObject> _root;
    StateSetStack _stateset;

    WriteVisitor(): osg::NodeVisitor(osg::NodeVisitor::TRAVERSE_ALL_CHILDREN) {}
    void write(std::ostream& str) {
        osg::ref_ptr<JSONObject> o = new JSONObject();
        o->getMaps()["Version"] = new JSONValue<int>(WRITER_VERSION);
        o->getMaps()["Generator"] = new JSONValue<std::string>("OpenSceneGraph " + std::string(osgGetVersion()) );
        o->getMaps()["osg.Node"] = _root.get();
        o->write(str);
    }

    void error() {
        throw "Error occur";
    }

    JSONObject* getParent() {
        if (_parents.empty()) {
            _root = new JSONObject;
            _parents.push_back(_root.get());
        }
        return _parents.back().get();
    }

    void apply(osg::Drawable& drw) {
        pushStateSet(drw);

        osg::Geometry* geom = dynamic_cast<osg::Geometry*>(&drw);
        if (geom) {
            if (needToSplit(*geom))
                error();
            osg::ref_ptr<JSONObject> json = new JSONNode;

            createJSONStateSet(drw, json);

            JSONObject* parent = getParent();
            if (parent)
                parent->addChild("osg.Geometry", json);

            if (!geom->getName().empty())
                json->getMaps()["Name"] = new JSONValue<std::string>(geom->getName());

            osg::ref_ptr<JSONObject> attributes = new JSONObject;

            int nbVertexes = geom->getVertexArray()->getNumElements();
            
            if (geom->getVertexArray()) {
                attributes->getMaps()["Vertex"] = new JSONBufferArray(geom->getVertexArray());
            }
            if (geom->getNormalArray()) {
                attributes->getMaps()["Normal"] = new JSONBufferArray(geom->getNormalArray());
                int nb = geom->getNormalArray()->getNumElements();
                if (nbVertexes != nb) {
                    osg::notify(osg::FATAL) << "Fatal nb normals " << nb << " != " << nbVertexes << std::endl;
                    error();
                }
            }
            if (geom->getColorArray()) {
                attributes->getMaps()["Color"] = new JSONBufferArray(geom->getColorArray());
                int nb = geom->getColorArray()->getNumElements();
                if (nbVertexes != nb) {
                    osg::notify(osg::FATAL) << "Fatal nb colors " << nb << " != " << nbVertexes << std::endl;
                    error();
                }
            }
            
            std::stringstream ss;
            for ( int i = 0; i < 32; i++) {
                ss.str("");
                ss << "TexCoord" << i;
                //osg::notify(osg::NOTICE) << ss.str() << std::endl;
                if (geom->getTexCoordArray(i)) {
                    attributes->getMaps()[ss.str()] = new JSONBufferArray(geom->getTexCoordArray(i));
                    int nb = geom->getTexCoordArray(i)->getNumElements();
                    if (nbVertexes != nb) {
                        osg::notify(osg::FATAL) << "Fatal nb tex coord " << i << " " << nb << " != " << nbVertexes << std::endl;
                        error();
                    }
                }
            }
            if (geom->getVertexAttribArray(TANGENT_ATTRIBUTE_INDEX)) {
                attributes->getMaps()["Tangent"] = new JSONBufferArray(geom->getVertexAttribArray(TANGENT_ATTRIBUTE_INDEX));
                int nb = geom->getVertexAttribArray(TANGENT_ATTRIBUTE_INDEX)->getNumElements();
                if (nbVertexes != nb) {
                    osg::notify(osg::FATAL) << "Fatal nb tangent " << nb << " != " << nbVertexes << std::endl;
                    error();
                }
            }

            if (geom->getVertexAttribArray(BITANGENT_ATTRIBUTE_INDEX)) {
                attributes->getMaps()["Bitangent"] = new JSONBufferArray(geom->getVertexAttribArray(BITANGENT_ATTRIBUTE_INDEX));
                int nb = geom->getVertexAttribArray(BITANGENT_ATTRIBUTE_INDEX)->getNumElements();
                if (nbVertexes != nb) {
                    osg::notify(osg::FATAL) << "Fatal nb bitangent " << nb << " != " << nbVertexes << std::endl;
                    error();
                }
            }

            json->getMaps()["VertexAttributeList"] = attributes;

            if (!geom->getPrimitiveSetList().empty()) {
                osg::ref_ptr<JSONArray> primitives = new JSONArray();
                for (unsigned int i = 0; i < geom->getPrimitiveSetList().size(); ++i) {

                    osg::ref_ptr<JSONObject> obj = new JSONObject;
                    if (geom->getPrimitiveSetList()[i]->getType() == osg::PrimitiveSet::DrawArraysPrimitiveType) {
                        osg::DrawArrays& da = dynamic_cast<osg::DrawArrays&>(*(geom->getPrimitiveSetList()[i]));
                        primitives->getArray().push_back(obj);
                        if (da.getMode() == GL_QUADS) {
                            obj->getMaps()["DrawElementsUShort"] = createJSONDrawElements(da);
                        } else {
                            obj->getMaps()["DrawArrays"] = new JSONDrawArray(da);
                        }

                    } else if (geom->getPrimitiveSetList()[i]->getType() == osg::PrimitiveSet::DrawElementsUIntPrimitiveType) {
                        osg::DrawElementsUInt& da = dynamic_cast<osg::DrawElementsUInt&>(*(geom->getPrimitiveSetList()[i]));
                        primitives->getArray().push_back(obj);
                        obj->getMaps()["DrawElementsUShort"] = new JSONDrawElements<osg::DrawElementsUInt>(da);

                    }  else if (geom->getPrimitiveSetList()[i]->getType() == osg::PrimitiveSet::DrawElementsUShortPrimitiveType) {
                        osg::DrawElementsUShort& da = dynamic_cast<osg::DrawElementsUShort&>(*(geom->getPrimitiveSetList()[i]));
                        primitives->getArray().push_back(obj);
                        obj->getMaps()["DrawElementsUShort"] = new JSONDrawElements<osg::DrawElementsUShort>(da);

                    }  else if (geom->getPrimitiveSetList()[i]->getType() == osg::PrimitiveSet::DrawElementsUBytePrimitiveType) {
                        osg::DrawElementsUByte& da = dynamic_cast<osg::DrawElementsUByte&>(*(geom->getPrimitiveSetList()[i]));
                        primitives->getArray().push_back(obj);
                        obj->getMaps()["DrawElementsUShort"] = new JSONDrawElements<osg::DrawElementsUByte>(da);

                    }  else if (geom->getPrimitiveSetList()[i]->getType() == osg::PrimitiveSet::DrawArrayLengthsPrimitiveType) {
                        osg::DrawArrayLengths& dal = dynamic_cast<osg::DrawArrayLengths&>(*(geom->getPrimitiveSetList()[i]));
                        primitives->getArray().push_back(obj);
                        obj->getMaps()["DrawArrayLengths"] = new JSONDrawArrayLengths(dal);

                    } else {
                        osg::notify(osg::WARN) << "Primitive Type " << geom->getPrimitiveSetList()[i]->getType() << " not supported, skipping" << std::endl;
                    }
                }
                json->getMaps()["PrimitiveSetList"] = primitives;
            }
        }
        popStateSet(drw);
    }

    void initJsonObjectFromNode(osg::Node& node, JSONObject& json) {
        if (!node.getName().empty())
            json.getMaps()["Name"] = new JSONValue<std::string>(node.getName());
    }

    void pushStateSet(osg::Node& node) {
        if (node.getStateSet())
            _stateset.push_back(node.getStateSet());
    }

    void pushStateSet(osg::Drawable& drw) {
        if (drw.getStateSet())
            _stateset.push_back(drw.getStateSet());
    }

    void popStateSet(osg::Drawable& drw) {
        if (drw.getStateSet())
            _stateset.pop_back();
    }

    void popStateSet(osg::Node& node) {
        if (node.getStateSet())
            _stateset.pop_back();
    }


    void createJSONStateSet(JSONObject* json, osg::StateSet* ss) {
        JSONObject* json_stateset = ::createJSONStateSet(ss);
        if (json_stateset) {
            JSONObject* obj = new JSONObject;
            obj->getMaps()["osg.StateSet"] = json_stateset;
            json->getMaps()["StateSet"] = obj;
        }
    }
    void createJSONStateSet(osg::Node& node, JSONObject* json) {
        if (node.getStateSet()) {
            createJSONStateSet(json, node.getStateSet());
        }
    }
    void createJSONStateSet(osg::Drawable& drw, JSONObject* json) {
        if (drw.getStateSet()) {
            createJSONStateSet(json, drw.getStateSet());
        }
    }

    void applyCallback(osg::Node& node, JSONObject* json) {
        JSONArray* updateCallbacks = new JSONArray;
        osg::NodeCallback* nc = node.getUpdateCallback();
        while (nc) {
            osgAnimation::BasicAnimationManager* am = dynamic_cast<osgAnimation::BasicAnimationManager*>(nc);
            if (am) {
                JSONArray* array = new JSONArray;
                JSONObject* bam = new JSONObject;
                bam->getMaps()["Animations"] = array;

                
                JSONObject* nodeCallbackObject = new JSONObject;
                nodeCallbackObject->getMaps()["osgAnimation.BasicAnimationManager"] = bam;
                updateCallbacks->getArray().push_back(nodeCallbackObject);
                
                for ( unsigned int i = 0; i < am->getAnimationList().size(); i++) {
                    osg::ref_ptr<JSONObject> jsonAnim = createJSONAnimation(am->getAnimationList()[i].get());
                    if (jsonAnim) {
                        JSONObject* obj = new JSONObject;
                        obj->getMaps()["osgAnimation.Animation"] = jsonAnim;
                        array->getArray().push_back(obj);
                        //std::stringstream ss;
                        //jsonAnim->write(ss);
                        //std::cout << ss.str() << std::endl;
                    }
                }
            } else {
                osgAnimation::UpdateMatrixTransform* updateMT = dynamic_cast<osgAnimation::UpdateMatrixTransform*>(nc);
                if (updateMT) {
                    osg::ref_ptr<JSONObject> jsonCallback = createJSONUpdateMatrixTransform(*updateMT);
                    if (jsonCallback.valid()) {
                        osg::ref_ptr<JSONObject> jsonObject = new JSONObject;
                        jsonObject->getMaps()["osgAnimation.UpdateMatrixTransform"] = jsonCallback;
                        updateCallbacks->getArray().push_back(jsonObject);
                    }
                }
            }
            nc = nc->getNestedCallback();
        }

        if (!updateCallbacks->getArray().empty()) {
            json->getMaps()["UpdateCallbacks"] = updateCallbacks;
        }
    }

    void apply(osg::Geode& node) {
        pushStateSet(node);

        if (_maps.find(&node) == _maps.end()) {
            osg::ref_ptr<JSONObject> json = new JSONNode;

            applyCallback(node, json);
            createJSONStateSet(node, json);

            JSONObject* parent = getParent();
            if (parent)
                parent->addChild("osg.Node", json);
            initJsonObjectFromNode(node, *json);
            _parents.push_back(json);
            for (unsigned int i = 0; i < node.getNumDrawables(); ++i) {
                if (node.getDrawable(i))
                    apply(*node.getDrawable(i));
            }
            _parents.pop_back();
        }

        popStateSet(node);
    }

    void apply(osg::Group& node) {
        pushStateSet(node);


        if (_maps.find(&node) == _maps.end()) {
            osg::ref_ptr<JSONObject> json = new JSONNode;

            applyCallback(node, json);
            createJSONStateSet(node, json);

            JSONObject* parent = getParent();
            if (parent)
                parent->addChild("osg.Node", json);

            initJsonObjectFromNode(node, *json);

            _parents.push_back(json);
            traverse(node);
            _parents.pop_back();
        }

        popStateSet(node);
    }

    void apply(osg::LightSource& node) {
        pushStateSet(node);

        if (_maps.find(&node) == _maps.end()) {
            osg::ref_ptr<JSONObject> json = new JSONNode;

            applyCallback(node, json);
            createJSONStateSet(node, json);

            JSONObject* parent = getParent();
            if (parent)
                parent->addChild("osg.LightSource", json);

            initJsonObjectFromNode(node, *json);

            if (node.getLight()) {
                JSONObject* obj = new JSONObject;
                obj->getMaps()["osg.Light"] = createLight(node.getLight());
                json->getMaps()["Light"] = obj;
            }

            _parents.push_back(json);
            traverse(node);
            _parents.pop_back();
        }

        popStateSet(node);
    }

    void apply(osg::Projection& node) {
        pushStateSet(node);

        if (_maps.find(&node) == _maps.end()) {
            osg::ref_ptr<JSONObject> json = new JSONNode;

            applyCallback(node, json);
            createJSONStateSet(node, json);

            JSONObject* parent = getParent();
            if (parent)
                parent->addChild("osg.Projection", json);

            initJsonObjectFromNode(node, *json);
            json->getMaps()["Matrix"] = new JSONMatrix(node.getMatrix());
            _parents.push_back(json);
            traverse(node);
            _parents.pop_back();
        }

        popStateSet(node);
    }

    void apply(osg::MatrixTransform& node) {
        pushStateSet(node);

        if (_maps.find(&node) == _maps.end()) {
            osg::ref_ptr<JSONObject> json = new JSONNode;

            applyCallback(node, json);
            createJSONStateSet(node, json);

            JSONObject* parent = getParent();
            if (parent)
                parent->addChild("osg.MatrixTransform", json);

            initJsonObjectFromNode(node, *json);
            json->getMaps()["Matrix"] = new JSONMatrix(node.getMatrix());
            _parents.push_back(json);
            traverse(node);
            _parents.pop_back();
        }

        popStateSet(node);
    }

    void apply(osg::PositionAttitudeTransform& node)
    {
        pushStateSet(node);

        if (_maps.find(&node) == _maps.end()) {
            osg::ref_ptr<JSONObject> json = new JSONNode;

            applyCallback(node, json);
            createJSONStateSet(node, json);

            JSONObject* parent = getParent();
            if (parent)
                parent->addChild("osg.MatrixTransform", json);
            initJsonObjectFromNode(node, *json);
            Matrix matrix = Matrix::identity();
            node.computeLocalToWorldMatrix(matrix,0);
            json->getMaps()["Matrix"] = new JSONMatrix(matrix);
            _parents.push_back(json);
            traverse(node);
            _parents.pop_back();
        }

        popStateSet(node);
    }

};


// the idea is to create true Geometry if skeleton with RigGeometry
struct FakeUpdateVisitor : public osgUtil::UpdateVisitor
{
    FakeUpdateVisitor() {
        setFrameStamp(new osg::FrameStamp());
    }
};

class ReaderWriterJSON : public osgDB::ReaderWriter
{
public:

     struct OptionsStruct {
         bool generateTangentSpace;
         int tangentSpaceTextureUnit;
         bool disableTriStrip;
         bool disableMergeTriStrip;
         int triStripCacheSize;
         bool useDrawArray;
         bool enableWireframe;

         OptionsStruct() {
             generateTangentSpace = false;
             tangentSpaceTextureUnit = 0;
             disableTriStrip = false;
             disableMergeTriStrip = false;
             triStripCacheSize = 16;
             useDrawArray = false;
             enableWireframe = false;
         }
    };


    ReaderWriterJSON()
    {
        supportsExtension("osgjs","OpenSceneGraph Javascript implementation format");
        supportsOption("buildTangentSpace","Build tangent space to each geometry");
        supportsOption("buildTangentSpaceTexUnit=<unit>","Specify on wich texture unit normal map is");
        supportsOption("triStripCacheSize=<int>","set the cache size when doing tristrip");
        supportsOption("disableMergeTriStrip","disable the merge of all tristrip into one");
        supportsOption("disableTriStrip","disable generation of tristrip");
        supportsOption("useDrawArray","prefer drawArray instead of drawelement with split of geometry");
        supportsOption("enableWireframe","create a wireframe geometry for each triangles geometry");
    }
        
    virtual const char* className() const { return "OSGJS json Writer"; }


    virtual WriteResult writeNode(const Node& node, const std::string& fileName, const osgDB::ReaderWriter::Options* options) const
    {
        std::string ext = osgDB::getFileExtension(fileName);
        if (!acceptsExtension(ext)) return WriteResult::FILE_NOT_HANDLED;

        {
            std::ofstream fout(fileName.c_str());
            if (fout)
            {
                WriteResult res = writeNode(node, fout, options);
                fout.close();
                return res;
            }
        }
        return WriteResult("Unable to open file for output");
    }

    virtual WriteResult writeNode(const Node& node, std::ostream& fout, const osgDB::ReaderWriter::Options* options) const
    {
        if (!fout) {
            return WriteResult("Unable to write to output stream");
        }

        OptionsStruct _options;
        _options = parseOptions(options);
        return writeNodeModel(node, fout, _options);
    }

    virtual WriteResult writeNodeModel(const Node& node, std::ostream& fout, const OptionsStruct& options) const
    {
        // process regular model
        osg::ref_ptr<osg::Node> model = osg::clone(&node);
        FakeUpdateVisitor fakeUpdateVisitor;
        model->accept(fakeUpdateVisitor);

        GeometryWireframeVisitor wireframer;
        if (options.enableWireframe) {
            model->accept(wireframer);
        }
            
        // generate model tangent space
        if (options.generateTangentSpace && options.enableWireframe == false) {
            TangentSpaceVisitor tgen(options.tangentSpaceTextureUnit);
            model->accept(tgen);
        }

        OpenGLESGeometryOptimizerVisitor visitor;
        visitor.setUseDrawArray(options.useDrawArray);
        visitor.setTripStripCacheSize(options.triStripCacheSize);
        visitor.setDisableTriStrip(options.disableTriStrip);
        visitor.setDisableMergeTriStrip(options.disableMergeTriStrip);
        model->accept(visitor);

        WriteVisitor writer;
        try {
            osgDB::writeNodeFile(*model, "/tmp/debug_osgjs.osg");
            model->accept(writer);
            if (writer._root.valid()) {
                writer.write(fout);
                return WriteResult::FILE_SAVED;
            }
        } catch (...){
            osg::notify(osg::FATAL) << "can't save osgjs file" << std::endl;
            return WriteResult("Unable to write to output stream");
        }
        return WriteResult("Unable to write to output stream");
    }

    ReaderWriterJSON::OptionsStruct parseOptions(const osgDB::ReaderWriter::Options* options) const
    {
        OptionsStruct localOptions;

        if (options)
        {
            osg::notify(NOTICE) << "options " << options->getOptionString() << std::endl;
            std::istringstream iss(options->getOptionString());
            std::string opt;
            while (iss >> opt)
            {
                // split opt into pre= and post=
                std::string pre_equals;
                std::string post_equals;

                size_t found = opt.find("=");
                if(found!=std::string::npos)
                {
                    pre_equals = opt.substr(0,found);
                    post_equals = opt.substr(found+1);
                } 
                else
                {
                    pre_equals = opt;
                }

                if (pre_equals == "useDrawArray")
                {
                    localOptions.useDrawArray = true;
                }
                if (pre_equals == "enableWireframe")
                {
                    localOptions.enableWireframe = true;
                }
                if (pre_equals == "disableMergeTriStrip")
                {
                    localOptions.disableMergeTriStrip = true;
                }
                if (pre_equals == "disableTriStrip")
                {
                    localOptions.disableTriStrip = true;
                }                
                if (pre_equals == "generateTangentSpace")
                {
                    localOptions.generateTangentSpace = true;
                }                

                if (post_equals.length()>0)
                {    
                    if (pre_equals == "tangentSpaceTextureUnit") {
                        localOptions.tangentSpaceTextureUnit = atoi(post_equals.c_str());
                    }
                    if (pre_equals == "triStripCacheSize") {
                        localOptions.triStripCacheSize = atoi(post_equals.c_str());
                    }
                }
            }
        }
        return localOptions;
    }
protected:

};

// now register with Registry to instantiate the above
// reader/writer.
REGISTER_OSGPLUGIN(osgjs, ReaderWriterJSON)
