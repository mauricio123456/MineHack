var Shadows = {}

Shadows.init = function(gl)
{
	var res = getProgram(gl, "shaders/shadow.fs", "shaders/shadow.vs");
	if(res[0] != "Ok")
	{
		return res[1];
	}
	
	Shadows.shadow_fs 		= res[1];
	Shadows.shadow_vs		= res[2];
	Shadows.shadow_shader	= res[3];
	
	Shadows.shadow_shader.pos_attr = gl.getAttribLocation(Shadows.shadow_shader, "pos");
	if(Shadows.shadow_shader.pos_attr == null)
		return "Could not locate position attribute";

	Shadows.shadow_shader.proj_mat = gl.getUniformLocation(Shadows.shadow_shader, "proj");
	if(Shadows.shadow_shader.proj_mat == null)
		return "Could not locate projection matrix uniform";
	
	Shadows.shadow_shader.view_mat = gl.getUniformLocation(Shadows.shadow_shader, "view");
	if(Shadows.shadow_shader.view_mat == null)
		return "Could not locate view matrix uniform";

	
	Shadows.shadow_maps = [ new ShadowMap(gl, 256, 256, 0.001) ];
	
	return "Ok";
}

//Retrieves a shadow map for a given level of detail
Shadows.get_shadow_map = function()
{

	return Shadows.shadow_maps[0];
}


//A shadow map
var ShadowMap = function(gl, width, height, clip_near, clip_far, z_bias)
{
	this.width		= width;
	this.height		= height;
	this.clip_near	= -1;
	this.clip_far	= 1;
	
	this.light_matrix = new Float32Array([ 1, 0, 0, 0,
										 0, 1, 0, 0,
										 0, 0, 1, 0,
										 0, 0, 0, 1 ]);

	this.shadow_tex = gl.createTexture();
	gl.bindTexture(gl.TEXTURE_2D, this.shadow_tex);
	gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
	gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
	gl.texParameteri(gl.TEXTURE_2D,	gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
	gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
	gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, width, height, 0, gl.RGBA, gl.FLOAT, null);
	gl.bindTexture(gl.TEXTURE_2D, null);
	
	this.depth_rb = gl.createRenderbuffer();
	gl.bindRenderbuffer(gl.RENDERBUFFER, this.depth_rb);
	gl.renderbufferStorage(gl.RENDERBUFFER, gl.DEPTH_COMPONENT16, width, height);
	gl.bindRenderbuffer(gl.RENDERBUFFER, null);
	
	this.fbo = gl.createFramebuffer();
	gl.bindFramebuffer(gl.FRAMEBUFFER, this.fbo);
	gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, this.shadow_tex, 0);
	gl.framebufferRenderbuffer(gl.FRAMEBUFFER, gl.DEPTH_ATTACHMENT, gl.RENDERBUFFER, this.depth_rb);
	
	if(!gl.isFramebuffer(this.fbo))
	{
		alert("Could not create shadow map frame buffer");
	}

	gl.bindFramebuffer(gl.FRAMEBUFFER, null);
}

ShadowMap.prototype.calc_light_matrix = function()
{
	//First generate all bounding points on the frustum
	var pts = [], 
		dx, dy, dz,
		camera = Game.camera_matrix(),
		T = m4inv(camera),
		W = new Float32Array(4), 
		P,
		basis = Sky.get_basis(),
		n = basis[0], u = basis[1], v = basis[2],
		i, j, k, l, s, 

		//Z-coordinate dimensions		
		z, z_max = 0.0, z_min, z_scale,
		
		//Dimensions for bounding square in uv plane
		side = 100000.0,
		ax = 0,
		ay = 1,
		cx = 0,
		cy = 0;
	
	//Construct the points for the frustum
	for(dx=-1; dx<=1; dx+=2)
	for(dy=-1; dy<=1; dy+=2)
	for(dz=this.clip_near; dz<=this.clip_far+0.001; dz+=this.clip_far-this.clip_near)
	{	
		W[0] = dx;
		W[1] = dy;
		W[2] = dz;
		W[3] = 1.0;
		
		P = hgmult(T, W);
		
		pts.push(new Float32Array([dot(P, u), dot(P, v)]));
		
		z = dot(P, n);
		z_max = Math.max(z_max, z);
	}
	
	//Compute minimal bounding square in uv plane
	for(i=0; i<pts.length; ++i)
	for(j=0; j<i; ++j)
	{
		dx = pts[i][0] - pts[j][0];
		dy = pts[i][1] - pts[j][1];
		
		l = Math.sqrt(dx*dx + dy*dy);
		
		if(l < 0.0001)
			continue;
		
		dx /= l;
		dy /= l;
		
		//Compute center of square and side length
		var x_min = 10000.0, x_max = -10000.0,
			y_min = 10000.0, y_max = -10000.0,
			px, py;
		
		for(k=0; k<pts.length; ++k)
		{
			px = dx * pts[k][0] + dy * pts[k][1];
			py = dy * pts[k][0] - dx * pts[k][1];
			
			x_min = Math.min(x_min, px);
			x_max = Math.max(x_max, px);
			y_min = Math.min(y_min, py);
			y_max = Math.max(y_max, py);
		}
		
		s  = Math.max(x_max - x_min, y_max - y_min);
			
		if(s < side)
		{
			side = s;
			ax	 = dx;
			ay	 = dy;
			cx	 = (x_min + x_max) / 2.0;
			cy	 = (y_min + y_max) / 2.0;
		}
	}
	
	//Time to build the light matrix!
	dx /= 0.5 * s;
	dy /= 0.5 * s;
	
	
	//FIXME: Need a better way to calculate this value...
	z_min = 0;
	
	z_scale = 2.0 / (z_max - z_min);
	
	return m4transp(new Float32Array([
		dx*u[0]+dy*v[0],	dx*u[1]+dy*v[1],	dx*u[2]+dy*v[2],	-cx/s,
		dy*u[0]-dx*v[0],	dy*u[1]-dx*v[1],	dy*u[2]-dx*v[2],	-cy/s,
		n[0]*z_scale,		n[1]*z_scale,		n[2]*z_scale,		-z_min*z_scale,
		0,					0,					0,					1]));

/*	
	return new Float32Array([
		0.01, 0, 0, 0,
		0, 0.01, 0, 0,
		0, 0, 0.01, 0,
		0, 0, 0, 1]);
*/
}


ShadowMap.prototype.begin = function(gl)
{

	//Calculate light matrix
	this.light_matrix = this.calc_light_matrix();

	gl.bindFramebuffer(gl.FRAMEBUFFER, this.fbo);
	gl.viewport(0, 0, this.width, this.height);
	
	gl.useProgram(Shadows.shadow_shader);
	
	gl.uniformMatrix4fv(Shadows.shadow_shader.proj_mat, false, this.light_matrix);

	gl.clearColor(0, 0, 1, 1);
	gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
	
	gl.disable(gl.BLEND);
	gl.enable(gl.DEPTH_TEST);
	
	/*
	//Only draw back faces for shadow map
	gl.frontFace(gl.CCW);
	gl.enable(gl.CULL_FACE);
	*/
	gl.disable(gl.CULL_FACE);
	
	gl.enableVertexAttribArray(Shadows.shadow_shader.pos_attr);
}

ShadowMap.prototype.end = function(gl)
{
	gl.bindFramebuffer(gl.FRAMEBUFFER, null);
	
	gl.disableVertexAttribArray(Shadows.shadow_shader.pos_attr);
	
	/*
	//Generate mipmap
	gl.bindTexture(gl.TEXTURE_2D, this.shadow_tex);
	gl.generateMipmap(gl.TEXTURE_2D);
	gl.bindTexture(gl.TEXTURE_2D, null);
	*/
}

ShadowMap.prototype.draw_debug = function(gl)
{
	Debug.draw_tex(this.shadow_tex);
}