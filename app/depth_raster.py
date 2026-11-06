"""Orthographic software z-buffer, shared by painting, picking and annotations.

Larger Camera.project depth is nearer the viewer. Pixel centres are sampled
at (x+.5,y+.5), and planar depth is interpolated rather than sorting polygons
by their mean. The local NumPy/Pillow runtime requires no GPU or network.
"""
import math
import numpy as np
from PIL import Image


def triangulate(points):
    """Ear clipping for simple concave faces; triangles/convex quads stay cheap."""
    count=len(points)
    if count<3:return ()
    if count==3:return ((0,1,2),)
    area=sum(a[0]*b[1]-b[0]*a[1] for a,b in zip(points,points[1:]+points[:1]))
    if abs(area)<1e-10:return ()
    sign=1 if area>0 else -1
    def cross(a,b,c):return ((b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]))*sign
    if all(cross(points[i-1],points[i],points[(i+1)%count])>=-1e-8 for i in range(count)):
        return tuple((0,i,i+1) for i in range(1,count-1))
    remaining=list(range(count));result=[]
    while len(remaining)>3:
        found=False
        for k,b in enumerate(remaining):
            a,c=remaining[k-1],remaining[(k+1)%len(remaining)]
            if cross(points[a],points[b],points[c])<=1e-9:continue
            occupied=False
            for j in remaining:
                if j in (a,b,c):continue
                if (cross(points[a],points[b],points[j])>1e-9 and
                    cross(points[b],points[c],points[j])>1e-9 and
                    cross(points[c],points[a],points[j])>1e-9):
                    occupied=True;break
            if occupied:continue
            result.append((a,b,c));remaining.pop(k);found=True;break
        if not found:
            raise ValueError('Kontur powierzchni widoku jest samoprzecinający lub zdegenerowany.')
    result.append(tuple(remaining))
    return tuple(result)


def _rgb(color):
    return tuple(int(color[i:i+2],16) for i in (1,3,5))+(255,)


class DepthRaster:
    def __init__(self,size,ids,scale=1):
        self.output_size=(max(1,int(size[0])),max(1,int(size[1])))
        self.width,self.height=(max(1,round(s*scale)) for s in self.output_size)
        self.scale_x,self.scale_y=self.width/self.output_size[0],self.height/self.output_size[1]
        self.ids=tuple(sorted(set(ids)));self.lookup={owner:i for i,owner in enumerate(self.ids)}
        self.depth=np.full((self.height,self.width),-np.inf,dtype=np.float32)
        self.owner=np.full((self.height,self.width),-1,dtype=np.int32)
        self.rgba=np.zeros((self.height,self.width,4),dtype=np.uint8)
        self.triangles=0

    def polygon(self,points,depths,color,owner):
        if len(points)!=len(depths):raise ValueError('Powierzchnia ma inną liczbę punktów i głębokości.')
        if self.scale_x!=1 or self.scale_y!=1:
            points=[(x*self.scale_x,y*self.scale_y) for x,y in points]
        if not points:return
        x0=max(0,math.ceil(min(p[0] for p in points)-.5));x1=min(self.width-1,math.floor(max(p[0] for p in points)-.5))
        y0=max(0,math.ceil(min(p[1] for p in points)-.5));y1=min(self.height-1,math.floor(max(p[1] for p in points)-.5))
        if x1<x0 or y1<y0:return
        # Near faces are submitted first. A covered bounding rectangle can
        # reject hidden shelves/hardware before allocating barycentric arrays.
        if np.min(self.depth[y0:y1+1,x0:x1+1])>max(depths)+.0005:return
        count=len(points)
        if count>3:
            cross=[(points[i][0]-points[i-1][0])*(points[(i+1)%count][1]-points[i][1])
                   -(points[i][1]-points[i-1][1])*(points[(i+1)%count][0]-points[i][0]) for i in range(count)]
            if all(v>=-1e-7 for v in cross) or all(v<=1e-7 for v in cross):
                self.convex(points,depths,color,owner)
                return
        for triangle in triangulate(points):
            self.triangle([points[i] for i in triangle],[depths[i] for i in triangle],color,owner)

    def convex(self,points,depths,color,owner):
        x0=max(0,math.ceil(min(p[0] for p in points)-.5));x1=min(self.width-1,math.floor(max(p[0] for p in points)-.5))
        y0=max(0,math.ceil(min(p[1] for p in points)-.5));y1=min(self.height-1,math.floor(max(p[1] for p in points)-.5))
        if x1<x0 or y1<y0:return
        first=points[0];triangle=None
        for i in range(1,len(points)-1):
            a,b=points[i],points[i+1]
            area=(a[0]-first[0])*(b[1]-first[1])-(a[1]-first[1])*(b[0]-first[0])
            if abs(area)>1e-8:triangle=(i,i+1,area);break
        if triangle is None:return
        i,j,area=triangle;a,b=points[i],points[j];sign=1 if area>0 else -1
        xs=np.arange(x0,x1+1,dtype=np.float32)[None,:]+.5
        ys=np.arange(y0,y1+1,dtype=np.float32)[:,None]+.5
        mask=np.ones((y1-y0+1,x1-x0+1),dtype=bool)
        for p,q in zip(points,points[1:]+points[:1]):
            mask &= ((q[0]-p[0])*(ys-p[1])-(q[1]-p[1])*(xs-p[0]))*sign>=-1e-5
        dx=((b[1]-first[1])*(depths[i]-depths[0])-(a[1]-first[1])*(depths[j]-depths[0]))/area
        dy=(-(b[0]-first[0])*(depths[i]-depths[0])+(a[0]-first[0])*(depths[j]-depths[0]))/area
        z=np.asarray(depths[0]+dx*(xs-first[0])+dy*(ys-first[1]),dtype=np.float32)
        self._write(x0,x1,y0,y1,mask,z,color,owner)
        self.triangles+=1

    def _write(self,x0,x1,y0,y1,mask,z,color,owner):
        target=self.depth[y0:y1+1,x0:x1+1];owners=self.owner[y0:y1+1,x0:x1+1]
        index=self.lookup[owner]
        equal=np.abs(z-target)<=.0005
        visible=mask&((z>target+.0005)|(equal&((owners<0)|(index<owners))))
        target[visible]=z[visible];owners[visible]=index
        self.rgba[y0:y1+1,x0:x1+1][visible]=_rgb(color)

    def triangle(self,points,depths,color,owner):
        (ax,ay),(bx,by),(cx,cy)=points
        area=(bx-ax)*(cy-ay)-(by-ay)*(cx-ax)
        if abs(area)<1e-9:return
        x0=max(0,math.ceil(min(ax,bx,cx)-.5));x1=min(self.width-1,math.floor(max(ax,bx,cx)-.5))
        y0=max(0,math.ceil(min(ay,by,cy)-.5));y1=min(self.height-1,math.floor(max(ay,by,cy)-.5))
        if x1<x0 or y1<y0:return
        # Broadcast rows/columns, avoiding a full meshgrid allocation per face.
        xs=np.arange(x0,x1+1,dtype=np.float32)[None,:]+.5-ax
        ys=np.arange(y0,y1+1,dtype=np.float32)[:,None]+.5-ay
        b=((cy-ay)*xs-(cx-ax)*ys)/area
        c=(-(by-ay)*xs+(bx-ax)*ys)/area
        mask=(b>=-1e-6)&(c>=-1e-6)&(b+c<=1.000001)
        z=np.asarray(depths[0]+b*(depths[1]-depths[0])+c*(depths[2]-depths[0]),dtype=np.float32)
        # Identical coplanar surfaces use stable owner ordering, so rotations
        # cannot change their identity according to polygon painter order.
        self._write(x0,x1,y0,y1,mask,z,color,owner)
        self.triangles+=1

    def line(self,points,depths,color,owner=None,width=1,ghost=False,tolerance=.6):
        if len(points)!=len(depths):raise ValueError('Obrys ma inną liczbę punktów i głębokości.')
        if self.scale_x!=1 or self.scale_y!=1:
            points=[(x*self.scale_x,y*self.scale_y) for x,y in points]
            width=max(1,width*min(self.scale_x,self.scale_y))
        index=self.lookup.get(owner,-1)
        for a,b,za,zb in zip(points,points[1:],depths,depths[1:]):
            dx,dy=b[0]-a[0],b[1]-a[1]
            if max(abs(dx),abs(dy))<.001:continue
            steps=min(max(self.width,self.height)*4,max(1,math.ceil(max(abs(dx),abs(dy)))))
            t=np.linspace(0,1,steps+1,dtype=np.float32)
            xs=np.floor(a[0]+dx*t).astype(np.int32);ys=np.floor(a[1]+dy*t).astype(np.int32)
            zs=za+(zb-za)*t
            radius=max(0,int(width)//2)
            for oy in range(-radius,radius+1):
                for ox in range(-radius,radius+1):
                    x,y=xs+ox,ys+oy;valid=(x>=0)&(x<self.width)&(y>=0)&(y<self.height)
                    x,y,z=x[valid],y[valid],zs[valid]
                    visible=(z>=self.depth[y,x]-tolerance)
                    if owner is not None and not ghost:visible&=(self.owner[y,x]==index)
                    x,y,z=x[visible],y[visible],z[visible]
                    self.rgba[y,x]=_rgb(color)
                    if ghost and index>=0:
                        self.owner[y,x]=index;self.depth[y,x]=z

    def image(self):
        result=Image.fromarray(self.rgba)
        if result.size!=self.output_size:result=result.resize(self.output_size,Image.Resampling.NEAREST)
        return result

    def outline_visible(self,selected_ids=()):
        """Cheap motion edges follow the same visible ownership as picking."""
        visible=self.owner>=0
        edge=np.zeros(self.owner.shape,dtype=bool)
        edge[1:,:] |= self.owner[1:,:]!=self.owner[:-1,:]
        edge[:-1,:] |= self.owner[:-1,:]!=self.owner[1:,:]
        edge[:,1:] |= self.owner[:,1:]!=self.owner[:,:-1]
        edge[:,:-1] |= self.owner[:,:-1]!=self.owner[:,1:]
        edge &= visible
        self.rgba[edge,:3]=(self.rgba[edge,:3].astype(np.float32)*.78).astype(np.uint8)
        for owner in selected_ids:
            index=self.lookup.get(owner)
            if index is not None:self.rgba[edge&(self.owner==index),:3]=(66,137,174)

    def attach(self,canvas):
        if (self.width,self.height)!=self.output_size:
            x=np.minimum(self.width-1,np.floor((np.arange(self.output_size[0])+.5)*self.scale_x).astype(np.int32))
            y=np.minimum(self.height-1,np.floor((np.arange(self.output_size[1])+.5)*self.scale_y).astype(np.int32))
            canvas._depth_buffer=self.depth[y[:,None],x[None,:]]
            canvas._id_buffer=self.owner[y[:,None],x[None,:]]
        else:
            canvas._depth_buffer=self.depth
            canvas._id_buffer=self.owner
        canvas._depth_ids=self.ids
        canvas._depth_sample_scale=min(self.scale_x,self.scale_y)


def pick_visible(canvas,x,y):
    native=getattr(canvas,'_native_pick',None)
    if native:return native(x,y)
    ids=getattr(canvas,'_id_buffer',None)
    if ids is None:return None
    x,y=int(math.floor(x)),int(math.floor(y))
    if not (0<=y<ids.shape[0] and 0<=x<ids.shape[1]):return None
    index=int(ids[y,x])
    return canvas._depth_ids[index] if index>=0 else None


def screen_visibility(canvas,x,y,depth=None,owner=None,tolerance=.8):
    native=getattr(canvas,'_native_depth',None)
    if native:
        if not (0<=x<canvas.winfo_width() and 0<=y<canvas.winfo_height()):return False
        hit=native(x,y)
        if hit is None:return True
        if depth is not None and depth<hit['depth']-tolerance:return False
        if owner is not None and depth is None and hit['id']!=owner:return False
        return True
    buffer=getattr(canvas,'_depth_buffer',None)
    if buffer is None:return True
    x,y=int(math.floor(x)),int(math.floor(y))
    if not (0<=y<buffer.shape[0] and 0<=x<buffer.shape[1]):return False
    surface=float(buffer[y,x])
    if depth is not None and depth<surface-tolerance:return False
    if owner is not None:
        visible=pick_visible(canvas,x,y)
        if visible is not None and visible!=owner and depth is None:return False
    return True


# Verified C++ calculation ports; UI and exact CAD validators are retained.
from native_model import install_services as _install_cpp
_install_cpp(globals(), 'depth_raster')
