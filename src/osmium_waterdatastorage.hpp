#include <geos/index/strtree/STRtree.h>

using namespace std;

typedef osmium::index::map::Dummy<osmium::unsigned_object_id_type, osmium::Location> index_neg_type;
typedef osmium::index::map::SparseMemArray<osmium::unsigned_object_id_type, osmium::Location> index_pos_type;
typedef osmium::handler::NodeLocationsForWays<index_pos_type, index_neg_type> location_handler_type;

class DataStorage {
    OGRDataSource* m_data_source;
    OGRLayer* m_layer_polygons;
    OGRLayer* m_layer_relations;
    OGRLayer* m_layer_ways;
    OGRLayer* m_layer_nodes;
    osmium::geom::OGRFactory<> m_ogr_factory;

    struct water_way {
        osmium::object_id_type firstnode;
        osmium::object_id_type lastnode;
        string name;

        water_way(osmium::object_id_type first_node, osmium::object_id_type last_node, const char *name_) {
            firstnode = first_node;
            lastnode = last_node;
            name = name_;
        }
    };

    vector<water_way*> water_ways;

    void init_db() {
        OGRRegisterAll();

        OGRSFDriver* driver = OGRSFDriverRegistrar::GetRegistrar()->GetDriverByName("SQLite");
        if (!driver) {
            cerr << "SQLite" << " driver not available.\n";
            exit(1);
        }

        CPLSetConfigOption("OGR_SQLITE_SYNCHRONOUS", "FALSE");
        const char* options[] = { "SPATIALITE=TRUE", nullptr };
        m_data_source = driver->CreateDataSource("/tmp/waterways.sqlite", const_cast<char**>(options));
        if (!m_data_source) {
            cerr << "Creation of output file failed.\n";
            exit(1);
        }

        OGRSpatialReference sparef;
        sparef.SetWellKnownGeogCS("WGS84");

        /*---- TABLE POLYGONS ----*/
        m_layer_polygons = m_data_source->CreateLayer("polygons", &sparef, wkbMultiPolygon, nullptr);
        if (!m_layer_polygons) {
            cerr << "Layer polygons creation failed.\n";
            exit(1);
        }

        OGRFieldDefn layer_polygons_field_way_id("way_id", OFTInteger);
        layer_polygons_field_way_id.SetWidth(12);
        if (m_layer_polygons->CreateField(&layer_polygons_field_way_id) != OGRERR_NONE) {
            cerr << "Creating way_id field failed.\n";
            exit(1);
        }

        OGRFieldDefn layer_polygons_field_relation_id("relation_id", OFTInteger);
        layer_polygons_field_relation_id.SetWidth(12);
        if (m_layer_polygons->CreateField(&layer_polygons_field_relation_id) != OGRERR_NONE) {
            cerr << "Creating relation_id field failed.\n";
            exit(1);
        }

        OGRFieldDefn layer_polygons_field_type("type", OFTString);
        layer_polygons_field_type.SetWidth(10);
        if (m_layer_polygons->CreateField(&layer_polygons_field_type) != OGRERR_NONE) {
            cerr << "Creating type field failed.\n";
            exit(1);
        }

        OGRFieldDefn layer_polygons_field_name("name", OFTString);
        layer_polygons_field_name.SetWidth(30);
        if (m_layer_polygons->CreateField(&layer_polygons_field_name) != OGRERR_NONE) {
            cerr << "Creating name field failed.\n";
            exit(1);
        }

        OGRFieldDefn layer_polygons_field_lastchange("lastchange", OFTString);
        layer_polygons_field_lastchange.SetWidth(20);
        if (m_layer_polygons->CreateField(&layer_polygons_field_lastchange) != OGRERR_NONE) {
            cerr << "Creating lastchange field failed.\n";
            exit(1);
        }

        OGRFieldDefn layer_polygons_field_error("error", OFTString);
        layer_polygons_field_error.SetWidth(6);
        if (m_layer_polygons->CreateField(&layer_polygons_field_error) != OGRERR_NONE) {
            cerr << "Creating error field failed.\n";
            exit(1);
        }

        if (m_layer_polygons->StartTransaction() != OGRERR_NONE) {
            cerr << "Creating polygons table failed.\n";
            cerr << m_layer_polygons->StartTransaction() << endl;
            exit(1);
        }


        /*---- TABLE RELATIONS ----*/
        m_layer_relations = m_data_source->CreateLayer("relations", &sparef, wkbMultiLineString, nullptr);
        if (!m_layer_relations) {
            cerr << "Layer relations creation failed.\n";
            exit(1);
        }

        OGRFieldDefn layer_relations_field_id("id", OFTInteger);
        layer_relations_field_id.SetWidth(12);
        if (m_layer_relations->CreateField(&layer_relations_field_id) != OGRERR_NONE) {
            cerr << "Creating id field failed.\n";
            exit(1);
        }

        OGRFieldDefn layer_relations_field_type("type", OFTString);
        layer_relations_field_type.SetWidth(10);
        if (m_layer_relations->CreateField(&layer_relations_field_type) != OGRERR_NONE) {
            cerr << "Creating type field failed.\n";
            exit(1);
        }

        OGRFieldDefn layer_relations_field_name("name", OFTString);
        layer_relations_field_type.SetWidth(30);
        if (m_layer_relations->CreateField(&layer_relations_field_name) != OGRERR_NONE) {
            cerr << "Creating name field failed.\n";
            exit(1);
        }

        OGRFieldDefn layer_relations_field_lastchange("lastchange", OFTString);
        layer_relations_field_lastchange.SetWidth(20);
        if (m_layer_relations->CreateField(&layer_relations_field_lastchange) != OGRERR_NONE) {
            cerr << "Creating lastchange field failed.\n";
            exit(1);
        }

        OGRFieldDefn layer_relations_field_nowaterway_error("nowaterway_error", OFTString);
        layer_relations_field_type.SetWidth(6);
        if (m_layer_relations->CreateField(&layer_relations_field_nowaterway_error) != OGRERR_NONE) {
            cerr << "Creating nowaterway_error field failed.\n";
            exit(1);
        }

        if (m_layer_relations->StartTransaction() != OGRERR_NONE) {
            cerr << "Creating relations table failed.\n";
            exit(1);
        }

        /*---- TABLE WAYS ----*/
        m_layer_ways = m_data_source->CreateLayer("ways", &sparef, wkbLineString, nullptr);
        if (!m_layer_ways) {
            cerr << "Layer ways creation failed.\n";
            exit(1);
        }

        OGRFieldDefn layer_ways_field_id("id", OFTInteger);
        layer_ways_field_id.SetWidth(12);
        if (m_layer_ways->CreateField(&layer_ways_field_id) != OGRERR_NONE) {
            cerr << "Creating id field failed.\n";
            exit(1);
        }

        OGRFieldDefn layer_ways_field_type("type", OFTString);
        layer_ways_field_type.SetWidth(10);
        if (m_layer_ways->CreateField(&layer_ways_field_type) != OGRERR_NONE) {
            cerr << "Creating type field failed.\n";
            exit(1);
        }

        OGRFieldDefn layer_ways_field_name("name", OFTString);
        layer_ways_field_name.SetWidth(30);
        if (m_layer_ways->CreateField(&layer_ways_field_name) != OGRERR_NONE) {
            cerr << "Creating name field failed.\n";
            exit(1);
        }

        OGRFieldDefn layer_ways_field_firstnode("firstnode", OFTString);
        layer_ways_field_firstnode.SetWidth(11);
        if (m_layer_ways->CreateField(&layer_ways_field_firstnode) != OGRERR_NONE) {
            cerr << "Creating firstnode field failed.\n";
            exit(1);
        }

        OGRFieldDefn layer_ways_field_lastnode("lastnode", OFTString);
        layer_ways_field_lastnode.SetWidth(11);
        if (m_layer_ways->CreateField(&layer_ways_field_lastnode) != OGRERR_NONE) {
            cerr << "Creating lastnode field failed.\n";
            exit(1);
        }

        OGRFieldDefn layer_ways_field_relation("relation", OFTInteger);
        layer_ways_field_relation.SetWidth(10);
        if (m_layer_ways->CreateField(&layer_ways_field_relation) != OGRERR_NONE) {
            cerr << "Creating relation field failed.\n";
            exit(1);
        }

        OGRFieldDefn layer_ways_field_width("width", OFTReal);
        layer_ways_field_width.SetWidth(10);
        if (m_layer_ways->CreateField(&layer_ways_field_width) != OGRERR_NONE) {
            cerr << "Creating width field failed.\n";
            exit(1);
        }

        OGRFieldDefn layer_ways_field_lastchange("lastchange", OFTString);
        layer_ways_field_lastchange.SetWidth(20);
        if (m_layer_ways->CreateField(&layer_ways_field_lastchange) != OGRERR_NONE) {
            cerr << "Creating lastchange field failed.\n";
            exit(1);
        }

        OGRFieldDefn layer_ways_field_width_error("width_error", OFTString);
        layer_ways_field_width_error.SetWidth(30);
        if (m_layer_ways->CreateField(&layer_ways_field_width_error) != OGRERR_NONE) {
            cerr << "Creating width_error field failed.\n";
            exit(1);
        }

        if (m_layer_ways->StartTransaction() != OGRERR_NONE) {
            cerr << "Creating ways table failed.\n";
            exit(1);
        }

        /*---- TABLE NODES ----*/
        m_layer_nodes = m_data_source->CreateLayer("nodes", &sparef, wkbPoint, nullptr);
        if (!m_layer_nodes) {
            cerr << "Layer nodes creation failed.\n";
            exit(1);
        }

        OGRFieldDefn layer_nodes_field_id("id", OFTString);
        layer_nodes_field_id.SetWidth(12);
        if (m_layer_nodes->CreateField(&layer_nodes_field_id) != OGRERR_NONE) {
            cerr << "Creating id field failed.\n";
            exit(1);
        }

        OGRFieldDefn layer_nodes_field_direction_error("direction_error", OFTString);
        layer_nodes_field_direction_error.SetWidth(30);
        if (m_layer_nodes->CreateField(&layer_nodes_field_direction_error) != OGRERR_NONE) {
            cerr << "Creating direction_error field failed.\n";
            exit(1);
        }

        OGRFieldDefn layer_nodes_field_name_error("name_error", OFTString);
        layer_nodes_field_name_error.SetWidth(30);
        if (m_layer_nodes->CreateField(&layer_nodes_field_name_error) != OGRERR_NONE) {
            cerr << "Creating name_error field failed.\n";
            exit(1);
        }

        if (m_layer_nodes->StartTransaction() != OGRERR_NONE) {
            cerr << "Creating nodes table failed.\n";
            exit(1);
        }
    }

    const char *get_waterway_type(const char *input) {
        if (!input) {
            return "";
        }
        if ((strcmp(input,"river"))&&(strcmp(input,"stream"))&&(strcmp(input,"drain"))
                &&(strcmp(input,"canal"))&&(strcmp(input,"riverbank"))) {
            return strdup("other\0");
        } else {
            return strdup(input);
        }
    }

    const string get_timestamp(osmium::Timestamp timestamp) {
        string output = timestamp.to_iso();
        output.replace(10, 1, " ");
        output.replace(19, 1, "");
        return output;
    }

    bool get_width(const char *width_chr, float &width) {
        string width_str = width_chr;
        bool err = false;

        if (width_str.find(",") != string::npos) {
            width_str.replace(width_str.find(","),1,".");
            err = true;
            width_chr = width_str.c_str();
        }

        char *endptr;
        width = strtof(width_chr, &endptr);

        if (endptr == width_chr) {
            width = -1;
            err = true;
        } else if (*endptr) {
           while(isspace(*endptr)) endptr++;
           if (!strcasecmp(endptr, "m")) {
           } else if (!strcasecmp(endptr, "km")) {
               width *=1000;
           } else if (!strcasecmp(endptr, "mi")) {
               width *=1609.344;
           } else if (!strcasecmp(endptr, "nmi")) {
               width *=1852;
           } else if (!strcmp(endptr, "'")) {
               width *= 12.0 * 0.0254;
           } else if (!strcmp(endptr, "\"")) {
               width *= 0.0254;
           } else if (*endptr == '\'') {
               endptr++;
               cout << "\n";
               char *inchptr;
               float inch = strtof(endptr,&inchptr);
               cout << inch << "\n";
               if ((!strcmp(inchptr, "\"")) && (endptr != inchptr)) {
                   width = (width * 12 + inch) * 0.0254;
               } else {
                   width = -1;
                   err = true;
               }
           } else {
               width = -1;
               err = true;
           }
        }
        return err;
    }

public:

    google::sparse_hash_map<osmium::object_id_type, vector<water_way*>> node_map;
    google::sparse_hash_map<osmium::object_id_type, long> direction_error_map;
    google::sparse_hash_map<osmium::object_id_type, long> name_error_map;
    geos::index::strtree::STRtree direction_error_tree;
    geos::index::strtree::STRtree name_error_tree;

    explicit DataStorage() {
        init_db();
        direction_error_map.set_deleted_key(-1);
        name_error_map.set_deleted_key(-1);
    }

    ~DataStorage() {
        m_layer_polygons->CommitTransaction();
        m_layer_relations->CommitTransaction();
        m_layer_ways->CommitTransaction();
        m_layer_nodes->CommitTransaction();
        OGRDataSource::DestroyDataSource(m_data_source);
        OGRCleanupAll();
        for (auto wway : water_ways) {
            delete wway;
        }
    }

    void insert_polygon_feature(OGRGeometry *geom, osmium::object_id_type way_id, osmium::object_id_type relation_id,
            const char *type, const char *name, osmium::Timestamp timestamp) {

        OGRFeature *feature = OGRFeature::CreateFeature(m_layer_polygons->GetLayerDefn());
        feature->SetGeometry(geom);
        feature->SetField("way_id", static_cast<int>(way_id));
        feature->SetField("relation_id", static_cast<int>(relation_id));
        feature->SetField("type", get_waterway_type(type));
        feature->SetField("name", name);
        feature->SetField("lastchange", get_timestamp(timestamp).c_str());
        if (m_layer_polygons->CreateFeature(feature) != OGRERR_NONE) {
            cerr << "Failed to create polygon feature.\n";
        }
        OGRFeature::DestroyFeature(feature);
    }

    void insert_relation_feature(OGRGeometry *geom, osmium::object_id_type id, const char *type,
            const char *name, osmium::Timestamp timestamp, bool contains_nowaterway) {

        OGRFeature *feature = OGRFeature::CreateFeature(m_layer_relations->GetLayerDefn());
        feature->SetGeometry(geom);
        feature->SetField("id", static_cast<int>(id));
        feature->SetField("type", get_waterway_type(type));
        feature->SetField("name", name);
        feature->SetField("lastchange", get_timestamp(timestamp).c_str());
        if (contains_nowaterway)
            feature->SetField("nowaterway_error", "true");
        else
            feature->SetField("nowaterway_error", "false");
        if (m_layer_relations->CreateFeature(feature) != OGRERR_NONE) {
            cerr << "Failed to create relation feature.\n";
        }
        OGRFeature::DestroyFeature(feature);
    }

    void insert_way_feature(OGRGeometry *geom, osmium::object_id_type id, const char *type,
            const char *name, osmium::object_id_type firstnode, osmium::object_id_type lastnode,
            osmium::object_id_type rel_id, const char *width, osmium::Timestamp timestamp) {

        OGRFeature *feature = OGRFeature::CreateFeature(m_layer_ways->GetLayerDefn());
        feature->SetGeometry(geom);
        feature->SetField("id", static_cast<int>(id));
        feature->SetField("type", get_waterway_type(type));
        feature->SetField("name", name);

        char firstnode_chr[13], lastnode_chr[13];
        sprintf(firstnode_chr, "%ld", firstnode);
        sprintf(lastnode_chr, "%ld", lastnode);

        water_way *wway = new water_way(firstnode,lastnode,((name) ? name : ""));  //HIER
        water_ways.push_back(wway);
        node_map[firstnode].push_back(wway);
        node_map[lastnode].push_back(wway);
        feature->SetField("firstnode", firstnode_chr);
        feature->SetField("lastnode", lastnode_chr);

        feature->SetField("relation", static_cast<int>(rel_id));
        feature->SetField("lastchange", get_timestamp(timestamp).c_str());
        bool width_err;
        if (width) {
            float w = 0;
            width_err = get_width(width,w);
            if (w>-1) feature->SetField("width", w);
        }
        if (width_err) {
            feature->SetField("width_error", "true");
        } else {
            feature->SetField("width_error", "false");
        }

        if (m_layer_ways->CreateFeature(feature) != OGRERR_NONE) {
            cerr << "Failed to create way feature.\n";
        }

        OGRFeature::DestroyFeature(feature);
    }

    void insert_node_feature(osmium::Location location, char *node_id, bool dir_err, bool name_err) {
        osmium::geom::OGRFactory<> ogr_factory;
        OGRFeature *node_feature = OGRFeature::CreateFeature(m_layer_nodes->GetLayerDefn());
        OGRPoint *node = ogr_factory.create_point(location).release();

        node_feature->SetGeometry(&*node);
        node_feature->SetField("id", node_id);

        if (dir_err) {
            node_feature->SetField("direction_error", "true");
        } else {
            node_feature->SetField("direction_error", "false");
        }

        if (name_err) {
            node_feature->SetField("name_error", "true");
        } else {
            node_feature->SetField("name_error", "false");
        }

        if (m_layer_nodes->CreateFeature(node_feature) != OGRERR_NONE) {
            cerr << "Failed to create node feature.\n";
        }
        OGRFeature::DestroyFeature(node_feature);
    }

    void change_bool_feature(char table, const long fid, const char *field, const char *value, char *error_advice) {
        OGRFeature *feature;
        OGRLayer *layer;
        switch (table) {
            case 'r':
                layer = m_layer_relations;
                break;
            case 'w':
                layer = m_layer_ways;
                break;
            case 'n':
                layer = m_layer_nodes;
                break;
            default:
                cerr << "change_bool_feature expects {'r','w','n'} = {relations, ways, nodes}" << endl;
                exit(1);
        }
        layer->GetFeature(fid);
        feature = layer->GetFeature(fid);
        feature->SetField(field, value);
        if (layer->SetFeature(feature) != OGRERR_NONE) {
            cerr << "Failed to change boolean field " << error_advice << endl;
        }
    }

    void init_trees(location_handler_type &locationhandler) {
        for (auto& node : direction_error_map) {
            osmium::geom::GEOSFactory<> geos_factory;
            geos::geom::Point *point = geos_factory.create_point(locationhandler.get_node_location(node.first)).release();
            direction_error_tree.insert(point->getEnvelopeInternal(), (osmium::object_id_type *) &(node.first));
        }
        for (auto& node : name_error_map) {
            osmium::geom::GEOSFactory<> geos_factory;
            geos::geom::Point *point = geos_factory.create_point(locationhandler.get_node_location(node.first)).release();
            name_error_tree.insert(point->getEnvelopeInternal(), (osmium::object_id_type *) &(node.first));
        }
    }
};
