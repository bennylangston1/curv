// Copyright 2016-2018 Doug Moen
// Licensed under the Apache License, version 2.0
// See accompanying file LICENSE or https://www.apache.org/licenses/LICENSE-2.0

#include <libcurv/function.h>
#include <libcurv/die.h>
#include <libcurv/dtostr.h>
#include <libcurv/gl_compiler.h>
#include <libcurv/gl_context.h>
#include <libcurv/geom/shape.h>

namespace curv { namespace geom {

void export_frag_2d(const Shape_Program&, std::ostream&);
void export_frag_3d(const Shape_Program&, std::ostream&);

void export_frag(const Shape_Program& shape, std::ostream& out)
{
    if (shape.is_2d_)
        return export_frag_2d(shape, out);
    if (shape.is_3d_)
        return export_frag_3d(shape, out);
    die("export_frag: shape is not 2d or 3d");
}

void export_frag_2d(const Shape_Program& shape, std::ostream& out)
{
    GL_Compiler gl(out, GL_Target::glsl);
    GL_Value dist_param = gl.newvalue(GL_Type::Vec4);

    out <<
        "#ifdef GLSLVIEWER\n"
        "uniform mat3 u_view2d;\n"
        "#endif\n"
        "float main_dist(vec4 " << dist_param << ", out vec4 colour)\n"
        "{\n";

    GL_Value result = shape.gl_dist(dist_param, gl);

    GL_Value colour = shape.gl_colour(dist_param, gl);
    out << "  colour = vec4(" << colour << ", 1.0);\n";

    out <<
        "  return " << result << ";\n"
        "}\n";
    BBox bbox = shape.bbox_;
    if (bbox.empty2() || bbox.infinite2()) {
        out <<
        "const vec4 bbox = vec4(-10.0,-10.0,+10.0,+10.0);\n";
    } else {
        out << "const vec4 bbox = vec4("
            << bbox.xmin << ","
            << bbox.ymin << ","
            << bbox.xmax << ","
            << bbox.ymax
            << ");\n";
    }
    out <<
        "void mainImage( out vec4 fragColour, in vec2 fragCoord )\n"
        "{\n"
        "    vec2 size = bbox.zw - bbox.xy;\n"
        "    vec2 scale2 = size / iResolution.xy;\n"
        "    vec2 offset = bbox.xy;\n"
        "    float scale;\n"
        "    if (scale2.x > scale2.y) {\n"
        "        scale = scale2.x;\n"
        "        offset.y -= (iResolution.y*scale - size.y)/2.0;\n"
        "    } else {\n"
        "        scale = scale2.y;\n"
        "        offset.x -= (iResolution.x*scale - size.x)/2.0;\n"
        "    }\n"
        "#ifdef GLSLVIEWER\n"
        "    fragCoord = (u_view2d * vec3(fragCoord,1)).xy;\n"
        "#endif\n"
        "    float d = main_dist(vec4(fragCoord*scale+offset,0,iTime), fragColour);\n"
        "    \n"
        "    // convert linear RGB to sRGB\n"
        "    fragColour.xyz = pow(fragColour.xyz, vec3(0.4545));\n"
        "    \n"
        "    if (d > 0.0) {\n"
        "        fragColour = vec4(1.0);\n" // white background
        "    }\n"
        "}\n"
        ;
}

void export_frag_3d(const Shape_Program& shape, std::ostream& out)
{
    GL_Compiler gl(out, GL_Target::glsl);

    GL_Value dist_param = gl.newvalue(GL_Type::Vec4);
    out <<
        "#ifdef GLSLVIEWER\n"
        "uniform vec3 u_eye3d;\n"
        "uniform vec3 u_centre3d;\n"
        "uniform vec3 u_up3d;\n"
        "#endif\n"
        "float dist(vec4 " << dist_param << ")\n"
        "{\n";
    GL_Value dist_result = shape.gl_dist(dist_param, gl);
    out <<
        "  return " << dist_result << ";\n"
        "}\n"
        "\n";

    GL_Value colour_param = gl.newvalue(GL_Type::Vec4);
    out <<
        "vec3 colour(vec4 " << colour_param << ")\n"
        "{\n";
    GL_Value colour_result = shape.gl_colour(colour_param, gl);
    out <<
        "  return " << colour_result << ";\n"
        "}\n"
        "\n";

    BBox bbox = shape.bbox_;
    if (bbox.empty3() || bbox.infinite3()) {
        out <<
        "const vec3 bbox_min = vec3(-10.0,-10.0,-10.0);\n"
        "const vec3 bbox_max = vec3(+10.0,+10.0,+10.0);\n";
    } else {
        out
        << "const vec3 bbox_min = vec3("
            << dfmt(bbox.xmin, dfmt::EXPR) << ","
            << dfmt(bbox.ymin, dfmt::EXPR) << ","
            << dfmt(bbox.zmin, dfmt::EXPR)
            << ");\n"
        << "const vec3 bbox_max = vec3("
            << dfmt(bbox.xmax, dfmt::EXPR) << ","
            << dfmt(bbox.ymax, dfmt::EXPR) << ","
            << dfmt(bbox.zmax, dfmt::EXPR)
            << ");\n";
    }

    // Following code is based on code fragments written by Inigo Quilez,
    // with The MIT Licence.
    //    Copyright 2013 Inigo Quilez
    out <<
       "// ray marching. ro is ray origin, rd is ray direction (unit vector).\n"
       "// result is (t,r,g,b), where\n"
       "//  * t is the distance that we marched,\n"
       "//  * r,g,b is the colour of the distance field at the point we ended up at.\n"
       "//    (-1,-1,-1) means no object was hit.\n"
       "vec4 castRay( in vec3 ro, in vec3 rd )\n"
       "{\n"
       "    float tmin = 1.0;\n"
       "    float tmax = 200.0;\n"
       "   \n"
       // TODO: implement bounding volume. If I remove the 'if(t>tmax)break'
       // check, then `tetrahedron` breaks. The hard coded tmax=200 fails for
       // some models.
       //"#if 0\n"
       //"    // bounding volume\n"
       //"    float tp1 = (0.0-ro.y)/rd.y; if( tp1>0.0 ) tmax = min( tmax, tp1 );\n"
       //"    float tp2 = (1.6-ro.y)/rd.y; if( tp2>0.0 ) { if( ro.y>1.6 ) tmin = max( tmin, tp2 );\n"
       //"                                                 else           tmax = min( tmax, tp2 ); }\n"
       //"#endif\n"
       //"    \n"
       "    float t = tmin;\n"
       "    vec3 c = vec3(-1.0,-1.0,-1.0);\n"
       "    for (int i=0; i<200; i++) {\n"
       "        float precis = 0.0005*t;\n"
       "        vec4 p = vec4(ro+rd*t,iTime);\n"
       "        float d = dist(p);\n"
       "        if (d < precis) {\n"
       "            c = colour(p);\n"
       "            break;\n"
       "        }\n"
       "        t += d;\n"
       "        if (t > tmax) break;\n"
       "    }\n"
       "    return vec4( t, c );\n"
       "}\n"

       "vec3 calcNormal( in vec3 pos )\n"
       "{\n"
       "    vec2 e = vec2(1.0,-1.0)*0.5773*0.0005;\n"
       "    return normalize( e.xyy*dist( vec4(pos + e.xyy,iTime) ) + \n"
       "                      e.yyx*dist( vec4(pos + e.yyx,iTime) ) + \n"
       "                      e.yxy*dist( vec4(pos + e.yxy,iTime) ) + \n"
       "                      e.xxx*dist( vec4(pos + e.xxx,iTime) ) );\n"
       //"    /*\n"
       //"    vec3 eps = vec3( 0.0005, 0.0, 0.0 );\n"
       //"    vec3 nor = vec3(\n"
       //"        dist(pos+eps.xyy) - dist(pos-eps.xyy),\n"
       //"        dist(pos+eps.yxy) - dist(pos-eps.yxy),\n"
       //"        dist(pos+eps.yyx) - dist(pos-eps.yyx) );\n"
       //"    return normalize(nor);\n"
       //"    */\n"
       "}\n"

       // Compute an ambient occlusion factor.
       // pos: point on surface
       // nor: normal of the surface at pos
       // Yields a value clamped to [0,1] where 0 means no other surfaces
       // around the point, and 1 means the point is occluded by other surfaces.
       "float calcAO( in vec3 pos, in vec3 nor )\n"
       "{\n"
       "    float occ = 0.0;\n"
       "    float sca = 1.0;\n"
       "    for( int i=0; i<5; i++ )\n"
       "    {\n"
       "        float hr = 0.01 + 0.12*float(i)/4.0;\n"
       "        vec3 aopos =  nor * hr + pos;\n"
       "        float dd = dist( vec4(aopos,iTime) );\n"
       "        occ += -(dd-hr)*sca;\n"
       "        sca *= 0.95;\n"
       "    }\n"
       "    return clamp( 1.0 - 3.0*occ, 0.0, 1.0 );    \n"
       "}\n"

       "// in ro: ray origin\n"
       "// in rd: ray direction\n"
       "// out: rgb colour\n"
       "vec3 render( in vec3 ro, in vec3 rd )\n"
       "{ \n"
       "    //vec3 col = vec3(0.7, 0.9, 1.0) +rd.z*0.8;\n"
       "    //vec3 col = vec3(0.8, 0.9, 1.0);\n"
       "    vec3 col = vec3(1.0, 1.0, 1.0);\n"
       "    vec4 res = castRay(ro,rd);\n"
       "    float t = res.x;\n"
       "    vec3 c = res.yzw;\n"
       "    if( c.x>=0.0 )\n"
       "    {\n"
       "        vec3 pos = ro + t*rd;\n"
       "        vec3 nor = calcNormal( pos );\n"
       "        vec3 ref = reflect( rd, nor );\n"
       "        \n"
       "        // material        \n"
       "        col = c;\n"
       "\n"
       "        // lighting        \n"
       "        float occ = calcAO( pos, nor );\n"
       "        vec3  lig = normalize( vec3(-0.4, 0.6, 0.7) );\n"
       "        float amb = clamp( 0.5+0.5*nor.z, 0.0, 1.0 );\n"
       "        float dif = clamp( dot( nor, lig ), 0.0, 1.0 );\n"
       "        float bac = clamp( dot( nor, normalize(vec3(-lig.x,lig.y,0.0))), 0.0, 1.0 )*clamp( 1.0-pos.z,0.0,1.0);\n"
       "        float dom = smoothstep( -0.1, 0.1, ref.z );\n"
       "        float fre = pow( clamp(1.0+dot(nor,rd),0.0,1.0), 2.0 );\n"
       "        float spe = pow(clamp( dot( ref, lig ), 0.0, 1.0 ),16.0);\n"
       "        \n"
       "        vec3 lin = vec3(0.0);\n"
       "        lin += 1.30*dif*vec3(1.00,0.80,0.55);\n"
       "        lin += 2.00*spe*vec3(1.00,0.90,0.70)*dif;\n"
       "        lin += 0.40*amb*vec3(0.40,0.60,1.00)*occ;\n"
       "        lin += 0.50*dom*vec3(0.40,0.60,1.00)*occ;\n"
       "        lin += 0.50*bac*vec3(0.35,0.35,0.35)*occ;\n"
       "        lin += 0.25*fre*vec3(1.00,1.00,1.00)*occ;\n"
       "        vec3 iqcol = col*lin;\n"
       "\n"
       "        //col = mix( col, vec3(0.8,0.9,1.0), 1.0-exp( -0.0002*t*t*t ) );\n"
       "        col = mix(col,iqcol, 0.4);\n"
       "    }\n"
       "\n"
       "    return vec3( clamp(col,0.0,1.0) );\n"
       "}\n"

       "// Create a matrix to transform coordinates to look towards a given point.\n"
       "// * `eye` is the position of the camera.\n"
       "// * `centre` is the position to look towards.\n"
       "// * `up` is the 'up' direction.\n"
       "mat3 look_at(vec3 eye, vec3 centre, vec3 up)\n"
       "{\n"
       "    vec3 ww = normalize(centre - eye);\n"
       "    vec3 uu = normalize(cross(ww, up));\n"
       "    vec3 vv = normalize(cross(uu, ww));\n"
       "    return mat3(uu, vv, ww);\n"
       "}\n"

       "// Generate a ray direction for ray-casting.\n"
       "// * `camera` is the camera look-at matrix.\n"
       "// * `pos` is the screen position, normally in the range -1..1\n"
       "// * `lens` is the lens length of the camera (encodes field-of-view).\n"
       "//   0 is very wide, and 2 is a good default.\n"
       "vec3 ray_direction(mat3 camera, vec2 pos, float lens)\n"
       "{\n"
       "    return normalize(camera * vec3(pos, lens));\n"
       "}\n"

       "void mainImage( out vec4 fragColor, in vec2 fragCoord )\n"
       "{\n"
       "    const vec3 origin = (bbox_min + bbox_max) / 2.0;\n"
       "    const vec3 radius = (bbox_max - bbox_min) / 2.0;\n"
       "    float r = max(radius.x, max(radius.y, radius.z)) / 1.3;\n"
       "    vec2 p = -1.0 + 2.0 * fragCoord.xy / iResolution.xy;\n"
       "    p.x *= iResolution.x/iResolution.y;\n"
       "\n"
       // convert from the OpenGL coordinate system to the Curv coord system.
       "#ifdef GLSLVIEWER\n"
       "    vec3 eye = vec3(u_eye3d.x, -u_eye3d.z, u_eye3d.y)*r + origin;\n"
       "    vec3 centre = vec3(u_centre3d.x, -u_centre3d.z, u_centre3d.y)*r + origin;\n"
       "    vec3 up = vec3(u_up3d.x, -u_up3d.z, u_up3d.y);\n"
       "#else\n"
       "    vec3 eye = vec3(2.6, -4.5, 3.0);\n"
       "    vec3 centre = vec3(0.0, 0.0, 0.0);\n"
       "    vec3 up = vec3(-0.25, 0.433, 0.866);\n"
       "#endif\n"
       "    mat3 camera = look_at(eye, centre, up);\n"
       "    vec3 dir = ray_direction(camera, p, 2.5);\n"
       "\n"
       "    vec3 col = render( eye, dir );\n"
       "    \n"
       "    // convert linear RGB to sRGB\n"
       "    col = pow(col, vec3(0.4545));\n"
       "    \n"
       "    fragColor = vec4(col,1.0);\n"
       "}\n"
       ;
}

}} // namespaces