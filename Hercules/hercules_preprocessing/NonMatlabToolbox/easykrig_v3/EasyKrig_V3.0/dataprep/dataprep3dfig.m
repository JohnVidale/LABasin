function fig = dataprep3dfig()
%% GUI configuration
%%
%%  Kriging Software Package  version 3.0,   May 1, 2004
%%  Copyright (c) 1999, 2001, 2004, property of Dezhang Chu and Woods Hole Oceanographic
%%  Institution.  All Rights Reserved.

global hdl para color

grey=[0.75 0.75 0.75];
dark_grey=[0.65 0.65 0.65];
if ~isempty(findobj('type','figure','Tag','DataPreparation'))
  figure(hdl.dataprep.h0);
  return
end
if para.platform == 2   % Unix
hdl.dataprep.h0= figure('Units','normalized', ...
	'Color',color.background, ...
	'Name','Data Preparation', ...
	'Tag','DataPreparation', ...
	'Position',hdl.window_position,'NumberTitle','off',...
    'Xvisual',para.Xvisual);
else
hdl.dataprep.h0= figure('Units','normalized', ...
	'Color',color.background, ...
	'Name','Data Preparation', ...
	'Tag','DataPreparation', ...
	'Position',hdl.window_position,'NumberTitle','off');
end
      set(0, 'showhidden', 'on')
      ch=get(gcf, 'children');
%	delete(ch(1))								%Help
   wmhdl=findobj(ch,'Label','&Help');		
   delete(wmhdl);
   ch(find(ch == wmhdl))=[];
%	delete(ch(3))								%Tools
%   wmhdl=findobj(ch,'Label','&Tools');		
%   delete(wmhdl);
%   ch(find(ch == wmhdl))=[];
%     new feature in V6.x delete(ch(6))			%Edit
   wmhdl=findobj(ch,'Label','&Edit');		
   delete(wmhdl);
   ch(find(ch == wmhdl))=[];
%      new feature in V6.x                      %insert
   wmhdl=findobj(ch,'Label','&Insert');		
   if ~isempty(wmhdl)
     delete(wmhdl);
     ch(find(ch == wmhdl))=[];
   end
%      new feature in V6.x                      %View
   wmhdl=findobj(ch,'Label','&View');		
   if ~isempty(wmhdl)
     delete(wmhdl);
     ch(find(ch == wmhdl))=[];
   end
% New feature in V7.0
    wmhdl=findobj(ch,'Label','&Desktop');	    %Desktop
   if ~isempty(wmhdl)
     delete(wmhdl);
     ch(find(ch == wmhdl))=[];
   end

Filehdl=findobj(ch,'Label','&File');
ch_file=get(Filehdl,'children');
%% ch_file   1  '&Print...'
%            2  'Print Pre&view...'
%            3  'Print Set&up...'
%            4  'Pa&ge Setup...'
%            5  'Pre&ferences...'
%            6  '&Export...'
%            7  'Save &As...'
%            8  '&Save'
%            9  '&Close'
%           10  '&Open...'
%           11  '&New Figure'
set(findobj(ch_file(1:end),'Label','&Open...'),'Label','&Load','callback','file_browser3d(1,2);')
file_hdl=findobj(ch_file,'Label','&New Figure');
if ~isempty(file_hdl) 
    delete(file_hdl);
    ch_file(find(ch_file == file_hdl))=[];	
end
set(findobj(ch_file(1:end),'Label','&Save'),'Label','Save &Figure As','callback','file_browser3d(4,3);')
file_hdl=findobj(ch_file,'Label','Save &As...');
delete(file_hdl);
ch_file(find(ch_file == file_hdl))=[];	
file_hdl=findobj(ch_file,'Label','&Export...');
if ~isempty(file_hdl) 
   delete(file_hdl);
   ch_file(find(ch_file == file_hdl))=[];	
end
file_hdl=findobj(ch_file,'Label','Pre&ferences...');
if ~isempty(file_hdl) 
   delete(file_hdl);
   ch_file(find(ch_file == file_hdl))=[];	
end

%delete(ch_file([11 8 7 6 5]));

h2(1)= uimenu('Parent',hdl.dataprep.h0,'label','&Task');
h2(2)=uimenu(h2(1),'label','   &Navigator','callback','main_menu3d','separator','off');
%h2(2)=uimenu(h2(1),'label','   &Load Data','callback','dataprep3dfig;','separator','off');
h2(3)=uimenu(h2(1),'label','   &Variogram','callback','variogram3dfig;','separator','on');
h2(4)=uimenu(h2(1),'label','   &Kriging','callback','kriging3dfig;','separator','off');
h2(5)=uimenu(h2(1),'label','   &Visualization','callback','dispkrig3dfig;','separator','off');
h2(6)= uimenu(h2(1),'label','&Save Window Position','callback','save_window_pos(hdl.dataprep.h0);','separator','on');

hdl.dataprep.help=uimenu(hdl.dataprep.h0,'label','&Help','separator','off');
hdl_dataprep_help_DFF=uimenu(hdl.dataprep.help,'label','Data File Format','separator','off','selected','off','checked','off');
uimenu(hdl_dataprep_help_DFF,'label','   X-axis','Callback','dataprep_help(2)','separator','off','checked','off');
uimenu(hdl_dataprep_help_DFF,'label','   Y-axis','Callback','dataprep_help(2)','separator','off','checked','off');
uimenu(hdl_dataprep_help_DFF,'label','   Z-axis','Callback','dataprep_help(2)','separator','off');
uimenu(hdl_dataprep_help_DFF,'label','   Column','Callback','dataprep_help(3)','separator','off');
uimenu(hdl_dataprep_help_DFF,'label','   Variable','Callback','dataprep_help(4)','separator','off');
uimenu(hdl_dataprep_help_DFF,'label','   Label','Callback','dataprep_help(5)','separator','off');
uimenu(hdl_dataprep_help_DFF,'label','   Unit','Callback','dataprep_help(6)','separator','off');
uimenu(hdl_dataprep_help_DFF,'label','   Direction','Callback','dataprep_help(7)','separator','off');
uimenu(hdl_dataprep_help_DFF,'label','   Save Data Format','Callback','dataprep_help(8)','separator','off');
hdl_dataprep_help_DR=uimenu(hdl.dataprep.help,'label','&Data Reduction','separator','on');
uimenu(hdl_dataprep_help_DR,'label','   Reduction Factor','Callback','dataprep_help(9)','separator','off');
uimenu(hdl_dataprep_help_DR,'label','   Filter Type','Callback','dataprep_help(10)','separator','off');
uimenu(hdl_dataprep_help_DR,'label','   Filter Support','Callback','dataprep_help(11)','separator','off');
uimenu(hdl.dataprep.help,'label','&External Program','Callback','dataprep_help(12)','separator','on');
uimenu(hdl.dataprep.help,'label','&Data Transformation','Callback','dataprep_help(13)','separator','on');
hdl_dataprep_help_PB=uimenu(hdl.dataprep.help,'label','&Data Display Type','separator','on');
uimenu(hdl_dataprep_help_PB,'label','   2D/3D Color-coded View','Callback','dataprep_help(15)','separator','off');
uimenu(hdl_dataprep_help_PB,'label','   Sample Sequence','Callback','dataprep_help(15)','separator','off');
uimenu(hdl.dataprep.help,'label','&File','Callback','dataprep_help(16)','separator','on');
hdl_dataprep_help_PB=uimenu(hdl.dataprep.help,'label','&Push Buttons','separator','on');
uimenu(hdl_dataprep_help_PB,'label','   Load','Callback','dataprep_help(17)','separator','off');
uimenu(hdl_dataprep_help_PB,'label','   Apply','Callback','dataprep_help(17)','separator','off');
uimenu(hdl_dataprep_help_PB,'label','   Navigator','Callback','dataprep_help(17)','separator','off');
uimenu(hdl_dataprep_help_PB,'label','   Quit','Callback','dataprep_help(17)','separator','off');

hdl_quit=uimenu(hdl.dataprep.h0,'label','Quit','separator','on');
uimenu(hdl_quit,'label','&Close Current Window','Callback','close_window(1)','separator','off');
uimenu(hdl_quit,'label','&Quit EasyKrig','Callback','close all','separator','off');


%% FILE ID
if isfield(para.dataprep,'fileID')
    fileIDstr=para.dataprep.fileID;
else
    fileIDstr=' ';
end
hdl.dataprep.fileID = uicontrol('Parent',hdl.dataprep.h0, ...
	'Units','normalized', ...
	'BackgroundColor',dark_grey, ...
	'FontWeight','bold', ...
	'ForegroundColor',[0 0 0.5], ...
	'Position',[0.27 0.96 0.48 0.03], ...
	'String',fileIDstr, ...
	'Style','text', ...
	'Tag','DataPrepFileID');

%% figure window
hdl.dataprep.axes1 = axes('Parent',hdl.dataprep.h0, ...
	'Color',[1 1 1], ...
	'Position',[0.1 0.54 0.5 0.4]);
hdl.dataprep.axes1_clrbar=colorbar;


% Data Format frame
h1 = uicontrol('Parent',hdl.dataprep.h0, ...
	'Units','normalized', ...
	'BackgroundColor',color.grey, ...
	'Position',[0.1 0.1 0.55 0.33], ...
	'Style','frame');

h1 = uicontrol('Parent',hdl.dataprep.h0, ...
	'Units','normalized', ...
	'BackgroundColor',color.dark_grey, ...
	'FontSize',9, ...
	'FontWeight','bold', ...
	'ForegroundColor',[0 0 1], ...
	'Position',[0.28  0.43  0.18 .028], ...
	'String','Data File Format', ...
	'Style','text');

h1 = uicontrol('Parent',hdl.dataprep.h0, ...
	'Units','normalized', ...
	'BackgroundColor',color.grey, ...
	'FontSize',9, ...
	'FontWeight','bold', ...
	'ForegroundColor',[0 0 0], ...
	'ListboxTop',0, ...
	'Position',[0.2  0.39   0.12 .028], ...
	'String','X - Axis', ...
	'Style','text');
h1 = uicontrol('Parent',hdl.dataprep.h0, ...
	'Units','normalized', ...
	'BackgroundColor',color.grey, ...
	'FontSize',9, ...
	'FontWeight','bold', ...
	'ListboxTop',0, ...
	'Position',[0.35  0.39   0.12 .028], ...
	'String','Y - Axis', ...
	'Style','text');
h1 = uicontrol('Parent',hdl.dataprep.h0, ...
	'Units','normalized', ...
	'BackgroundColor',color.grey, ...
	'FontSize',9, ...
	'FontWeight','bold', ...
	'Position',[0.5  0.39   0.12 .028], ...
	'String','Z - Axis', ...
	'Style','text');


%%% Data Column
hdl.dataprep.axis = uicontrol('Parent',hdl.dataprep.h0, ...
	'Units','normalized', ...
	'BackgroundColor',color.grey, ...
	'FontWeight','bold', ...
	'HorizontalAlignment','center', ...
	'Position',[0.12 0.345 0.10 0.03], ...
	'String','Column', ...
	'Style','text', ...
	'Tag','Axis');
hdl.dataprep.x_axis = uicontrol('Parent',hdl.dataprep.h0, ...
	'Units','normalized', ...
	'BackgroundColor',[1 1 1], ...
	'Callback','popupmenu_action(1,1)', ...
	'Position',[0.2 0.35 0.12 0.03], ...
	'String',{'Data Col. 1', 'Data Col. 2', 'Data Col. 3'}, ...
	'Style','popupmenu', ...
	'Tag','x_axis', ...
	'Value',2);
hdl.dataprep.y_axis = uicontrol('Parent',hdl.dataprep.h0, ...
	'Units','normalized', ...
	'BackgroundColor',[1 1 1], ...
	'Callback','popupmenu_action(1,2)', ...
	'Position',[0.35 0.35 0.12 0.03], ...
	'String',{'Data Col. 1', 'Data Col. 2', 'Data Col. 3'}, ...
	'Style','popupmenu', ...
	'Tag','y_axis', ...
	'Value',1);
hdl.dataprep.z_axis = uicontrol('Parent',hdl.dataprep.h0, ...
	'Units','normalized', ...
	'BackgroundColor',[1 1 1], ...
	'Callback','popupmenu_action(1,3)', ...
	'Position',[0.50 0.35 0.12 0.03], ...
	'String',{'Data Col. 1', 'Data Col. 2', 'Data Col. 3'}, ...
	'Style','popupmenu', ...
	'Tag','z_axis', ...
	'Value',3);

%% Varibles
h1 = uicontrol('Parent',hdl.dataprep.h0, ...
	'Units','normalized', ...
	'BackgroundColor',color.grey, ...
	'FontWeight','bold', ...
	'HorizontalAlignment', 'center', ...
	'Position',[0.12 0.295 0.08 0.03], ...
	'String','Variable', ...
	'Style','text');
opt_str={'LONGITUDE','LATITUDE','DEPTH','X','Y','TIME','OTHER'};
hdl.dataprep.var1 = uicontrol('Parent',hdl.dataprep.h0, ...
	'Units','normalized', ...
	'BackgroundColor',[1 1 1], ...
	'Callback','popupmenu_action(1,4)', ...
	'Position',[0.2    0.30    0.12    0.03], ...
	'String',opt_str, ...
	'Tag','Var1', ...
	'Style','popupmenu', ...
	'Value',1);
hdl.dataprep.var2 = uicontrol('Parent',hdl.dataprep.h0, ...
	'Units','normalized', ...
	'BackgroundColor',[1 1 1], ...
	'Callback','popupmenu_action(1,5)', ...
	'Position',[0.35    0.30    0.12    0.03], ...
	'String',opt_str, ...
	'Style','popupmenu', ...
	'Tag','Var2', ...
	'Value',2);
hdl.dataprep.var3 = uicontrol('Parent',hdl.dataprep.h0, ...
	'Units','normalized', ...
	'BackgroundColor',[1 1 1], ...
	'Callback','popupmenu_action(1,6)', ...
	'Position',[0.50    0.30    0.12    0.03], ...
	'String',opt_str, ...
	'Style','popupmenu', ...
	'Tag','Var3', ...
	'Value',3);

%% Labels
h1 = uicontrol('Parent',hdl.dataprep.h0, ...
	'Units','normalized', ...
	'BackgroundColor',color.grey, ...
	'FontWeight','bold', ...
	'HorizontalAlignment', 'center', ...
	'Position',[0.12 0.245 0.08 0.03], ...
	'String','Label', ...
	'Style','text');
hdl.dataprep.xlabel = uicontrol('Parent',hdl.dataprep.h0, ...
	'Units','normalized', ...
	'BackgroundColor',[1 1 1], ...
	'HorizontalAlignment','left', ...
	'Position',[0.2 0.25 0.12 0.03], ...
	'String','LONGITUDE', ...
	'Style','edit', ...
	'Tag','xLabel');
hdl.dataprep.ylabel = uicontrol('Parent',hdl.dataprep.h0, ...
	'Units','normalized', ...
	'BackgroundColor',[1 1 1], ...
	'HorizontalAlignment','left', ...
	'Position',[0.35 0.25 0.12 0.03], ...
	'String','LATITUDE', ...
	'Style','edit', ...
	'Tag','yLabel');
hdl.dataprep.zlabel = uicontrol('Parent',hdl.dataprep.h0, ...
	'Units','normalized', ...
	'BackgroundColor',[1 1 1], ...
	'HorizontalAlignment','left', ...
	'Position',[0.5 0.25 0.12 0.03], ...
	'String','DEPTH', ...
	'Style','edit', ...
	'Tag','zLabel');

%% Units
unit_str={'(deg)','(km)','(m)','(cm)','(mm)','(Day)','(hour)','(min)','(sec)','(other)'};
h1 = uicontrol('Parent',hdl.dataprep.h0, ...
	'Units','normalized', ...
	'BackgroundColor',color.grey, ...
	'FontWeight','bold', ...
	'HorizontalAlignment', 'center', ...
	'Position',[0.12 0.195 0.08 0.03], ...
	'String','Unit', ...
   'Style','text');
hdl.dataprep.x_unit = uicontrol('Parent',hdl.dataprep.h0, ...
	'Units','normalized', ...
	'BackgroundColor',[1 1 1], ...
	'HorizontalAlignment','left', ...
	'Position',[0.2 0.20 0.12 0.03], ...
	'String',unit_str, ...
	'Style','popupmenu', ...
	'Tag','xUnit','value',1);
hdl.dataprep.y_unit = uicontrol('Parent',hdl.dataprep.h0, ...
	'Units','normalized', ...
	'BackgroundColor',[1 1 1], ...
	'HorizontalAlignment','left', ...
	'Position',[0.35 0.20 0.12 0.03], ...
	'String',unit_str, ...
	'Style','popupmenu', ...
	'Tag','yUnit','value',1);
hdl.dataprep.z_unit = uicontrol('Parent',hdl.dataprep.h0, ...
	'Units','normalized', ...
	'BackgroundColor',[1 1 1], ...
	'HorizontalAlignment','left', ...
	'Position',[0.50 0.20 0.12 0.03], ...
	'String',unit_str, ...
	'Style','popupmenu', ...
   'Tag','zUnit',...
   'Value',3);

%% Direction
h1 = uicontrol('Parent',hdl.dataprep.h0, ...
	'Units','normalized', ...
	'BackgroundColor',color.grey, ...
	'FontWeight','bold', ...
	'HorizontalAlignment', 'center', ...
	'Position',[0.12 0.145 0.08 0.03], ...
	'String','Direction', ...
	'Style','text');

hdl.dataprep.xdir = uicontrol('Parent',hdl.dataprep.h0, ...
	'Units','normalized', ...
	'BackgroundColor',color.grey, ...
	'Callback','radio_action(1,2)', ...
	'FontWeight','bold', ...
	'Position',[0.22 0.15 0.12 0.03], ...
	'String','Reverse', ...
	'Style','radio');
hdl.dataprep.ydir = uicontrol('Parent',hdl.dataprep.h0, ...
	'Units','normalized', ...
	'BackgroundColor',color.grey, ...
	'Callback','radio_action(1,3)', ...
	'FontWeight','bold', ...
	'Position',[0.37 0.15 0.12 0.03], ...
	'String','Reverse', ...
	'Style','radio');
hdl.dataprep.zdir = uicontrol('Parent',hdl.dataprep.h0, ...
	'Units','normalized', ...
	'BackgroundColor',color.grey, ...
	'Callback','radio_action(1,4)', ...
	'FontWeight','bold', ...
	'Position',[0.52 0.15 0.12 0.03], ...
	'String','Reverse', ...
	'Style','radio', ...
    'value',1);

%% save data format file
hdl.dataprep.data_format = uicontrol('Parent',hdl.dataprep.h0, ...
	'Units','normalized', ...
	'FontSize',8, ...
	'BackgroundColor',color.grey, ...
	'Callback','radio_action(1,5)', ...
	'FontWeight','bold', ...
	'Position',[0.22 0.11 0.12 0.03], ...
	'String','Save Data Format', ...
	'Style','radio');
hdl.dataprep.data_format_browser = uicontrol('Parent',hdl.dataprep.h0, ...
	'Units','normalized', ...
	'Callback','file_browser3d(1,3)', ...
	'FontSize',8, ...
	'Position',[0.37 0.11  0.08  0.03], ...
	'String','Browse', ...
   'Enable','off', ...
	'Tag','DataFormatFileBrowser');

% Data Reduction Processing
x0=0.68;y0=0.48;xL=0.27;yL=0.47;dy=0.028;
h1 = uicontrol('Parent',hdl.dataprep.h0, ...
	'Units','normalized', ...
	'BackgroundColor',color.grey, ...
	'Position',[x0 y0 xL yL], ...
	'Style','frame', ...
	'Tag','FrameDataReduction');
h1 = uicontrol('Parent',hdl.dataprep.h0, ...
	'Units','normalized', ...
	'BackgroundColor',color.background, ...
	'Position',[0.70 0.71 0.23 0.2], ...
	'Style','frame', ...
	'Tag','FrameDataReduction');
h1 = uicontrol('Parent',hdl.dataprep.h0, ...
	'Units','normalized', ...
	'BackgroundColor',color.grey, ...
	'FontSize',9, ...
	'FontWeight','bold', ...
	'ForegroundColor',[0 0 1], ...
	'Position',[0.71 0.91 0.2 dy], ...
	'String','Data Reduction', ...
	'Style','text');
h1 = uicontrol('Parent',hdl.dataprep.h0, ...
	'Units','normalized', ...
	'BackgroundColor',color.grey, ...
	'FontWeight','bold', ...
	'HorizontalAlignment', 'center', ...
	'Position',[0.71 0.85 0.13 0.03], ...
	'String','Reduction Factor', ...
	'Style','text');
hdl.dataprep.reduct_fac = uicontrol('Parent',hdl.dataprep.h0, ...
	'Units','normalized', ...
	'BackgroundColor',[1 1 1], ...
	'HorizontalAlignment','left', ...
	'Position',[0.85 0.85 0.05 0.03], ...
	'String','1', ...
	'Style','edit', ...
	'Tag','ReductionFactor');
h1 = uicontrol('Parent',hdl.dataprep.h0, ...
	'Units','normalized', ...
	'BackgroundColor',color.grey, ...
	'FontWeight','bold', ...
	'Position',[0.71 0.80 0.10 0.03], ...
	'String','Filter', ...
	'HorizontalAlignment', 'center', ...
	'Style','text');
str_opt={'Simple','Mean','Median'};
hdl.dataprep.filter = uicontrol('Parent',hdl.dataprep.h0, ...
	'Units','normalized', ...
	'Callback','popupmenu_action(1,7)', ...
	'BackgroundColor',[1 1 1], ...
	'Position',[0.82 0.80 0.08 0.03], ...
	'String',str_opt, ...
	'Style','popupmenu', ...
	'Tag','Filter', ...
	'Value',2);
h1 = uicontrol('Parent',hdl.dataprep.h0, ...
	'Units','normalized', ...
	'BackgroundColor',color.grey, ...
	'FontWeight','bold', ...
	'Position',[0.71 0.75 0.13 0.03], ...
	'HorizontalAlignment', 'center', ...
	'String','Support', ...
	'Style','text');
hdl.dataprep.filter_supt = uicontrol('Parent',hdl.dataprep.h0, ...
	'Units','normalized', ...
	'BackgroundColor',[1 1 1], ...
	'HorizontalAlignment','left', ...
	'Position',[0.85 0.75 0.05 0.03], ...
	'String','1', ...
	'Style','edit', ...
	'Tag','FilterSupport');
%% external program
hdl.dataprep.ext_prog = uicontrol('Parent',hdl.dataprep.h0, ...
	'Units','normalized', ...
	'BackgroundColor',color.grey, ...
	'Callback','radio_action(1,1)', ...
	'FontWeight','bold', ...
	'ForegroundColor',[0 0 1], ...
	'Position',[0.70  0.62 0.15 0.03], ...
	'String','External Program', ...
	'Style','radio', ...
	'Tag','program');
hdl.dataprep.dat_conv = uicontrol('Parent',hdl.dataprep.h0, ...
	'Units','normalized', ...
	'Callback','file_browser3d(1,1)', ...
	'FontSize',8, ...
	'Position',[0.85 0.625  0.08  0.03], ...
	'String','Browse', ...
   'Enable','off', ...
	'Tag','ExtFileBrowser');
hdl.dataprep.dat_conv_fname = uicontrol('Parent',hdl.dataprep.h0, ...
	'Units','normalized', ...
	'BackgroundColor',[1 1 1], ...
	'HorizontalAlignment','left', ...
	'Position',[.70 0.58 0.23 0.03], ...
	'String','', ...
	'Style','edit', ...
    'Visible','off', ...
	'Tag','ExtProgram');

% Data Transformation
x0=0.68;y0=0.47;dy=0.03;
h1 = uicontrol('Parent',hdl.dataprep.h0, ...
	'Units','normalized', ...
	'BackgroundColor',color.grey, ...
	'FontSize',9, ...
	'FontWeight','bold', ...
	'ForegroundColor',[0 0 1], ...
	'HorizontalAlignment', 'left', ...
	'Position',[x0+0.02 y0+0.06 0.15 dy], ...
	'String','Data Transformation', ...
	'Style','text');
opt_str={'none','log10(z+1)','ln(z+1)','10log10(z+1)','10ln(z+1)','log10(z)','ln(z)',};
hdl.dataprep.transform = uicontrol('Parent',hdl.dataprep.h0, ...
	'Units','normalized', ...
	'BackgroundColor',[1 1 1], ...
	'callback', 'popupmenu_action(1,8)', ...
	'Position',[x0+0.18 y0+0.06 0.07 dy], ...
	'String',opt_str, ...
	'Style','popupmenu', ...
	'Tag','TransformOpt', ...
	'Value',1);

if 0
%% axes conversion
hdl.dataprep.axes_conv = uicontrol('Parent',hdl.dataprep.h0, ...
	'Units','normalized', ...
	'Callback','check_unitsfig(2);', ...
	'FontWeight','bold', ...
	'ForegroundColor',[0 0 1], ...
	'Position',[0.73 0.45 0.15 0.04], ...
	'String','Axes Conversion', ...
    'Enable','on', ...
	'Tag','AxesConv');
end
% Filename
h1 = uicontrol('Parent',hdl.dataprep.h0, ...
	'Units','normalized', ...
	'BackgroundColor',color.grey, ...
	'FontSize',8, ...
	'FontWeight','bold', ...
	'HorizontalAlignment','center', ...
	'Position',[0.1 0.06 0.05 0.03], ...
	'String','File: ', ...
	'Style','text');
hdl.dataprep.file = uicontrol('Parent',hdl.dataprep.h0, ...
	'Units','normalized', ...
	'BackgroundColor',color.grey, ...
	'HorizontalAlignment','left', ...
	'Position',[0.152 0.06 0.5 0.03], ...
	'Style','text', ...
	'Tag','File');

% Display Type
x0=0.68;y0=0.30;xL=0.27;yL=0.12;dy=0.028;
h1 = uicontrol('Parent',hdl.dataprep.h0, ...
	'Units','normalized', ...
	'BackgroundColor',color.grey, ...
	'Position',[x0 y0 xL yL], ...
	'Style','frame', ...
	'Tag','FrameDisplayType');
h1 = uicontrol('Parent',hdl.dataprep.h0, ...
	'Units','normalized', ...
	'BackgroundColor',color.grey, ...
	'FontSize',9, ...
	'FontWeight','bold', ...
	'ForegroundColor',[0 0 1], ...
	'Position',[x0+0.08 y0+yL-0.015 0.1 0.03], ...
	'String','Display Type', ...
	'Style','text');
hdl.dataprep.data_type1 = uicontrol('Parent',hdl.dataprep.h0, ...
	'Units','normalized', ...
	'BackgroundColor',color.grey, ...
	'Callback','radio_action(1,6)', ...
	'FontWeight','bold', ...
	'Position',[x0+0.05 y0+yL-0.06 0.18 0.03], ...
	'String','2D/3D Color-coded View', ...
	'Style','radio', ...
    'value',1);
hdl.dataprep.data_type2 = uicontrol('Parent',hdl.dataprep.h0, ...
	'Units','normalized', ...
	'BackgroundColor',color.grey, ...
	'Callback','radio_action(1,7)', ...
	'FontWeight','bold', ...
	'Position',[x0+0.05 y0+yL-0.1 0.18 0.03], ...
	'String','Sample Sequence', ...
	'Style','radio', ...
    'value',0);


%% Pushbutton for Action
hdl.dataprep.h1 = uicontrol('Parent',hdl.dataprep.h0, ...
	'Units','normalized', ...
	'Callback','file_browser3d(1,2)', ...
	'FontSize',10, ...
	'FontWeight','bold', ...
	'Position',[0.70 0.17  0.1  0.04], ...
	'String','Load', ...
	'Tag','Loading');
h1 = uicontrol('Parent',hdl.dataprep.h0, ...
	'Units','normalized', ...
	'Callback','dataprep3d(1)', ...
	'FontSize',10, ...
	'FontWeight','bold', ...
	'Position',[0.70 0.12 0.1  0.04], ...
	'String','Apply', ...
	'Tag','Action');
h1 = uicontrol('Parent',hdl.dataprep.h0, ...
	'Units','normalized', ...
	'Callback','figure(hdl.navigator.h0)', ...
	'FontSize',10, ...
	'FontWeight','bold', ...
	'Position',[0.81  0.17    0.1    0.04], ...
	'String','Navigator', ...
	'Tag','Navigator');
h1 = uicontrol('Parent',hdl.dataprep.h0, ...
	'Units','normalized', ...
	'Callback','close_window(1)', ...
	'FontSize',10, ...
	'FontWeight','bold', ...
	'Position',[0.81 0.12  0.1  0.04], ...
	'String','Quit', ...
	'Tag','Quit');

para.status.dataprepfig = 1;
hdl.status.dataprepfig = 1;



