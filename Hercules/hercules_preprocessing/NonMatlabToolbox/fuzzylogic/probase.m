function [m,s]=probase(res,bas,limres)
% CALCULA LA MEDIA Y LA DESVIACION ESTANDAR DE RECONOCIMIENTO
% PARA LA FUNCION DE PROBABILIDAD DIFUSA.
% LAS SERIES DE ENTRADA SON DOS, LA SERIE DE SALIDA res Y
% LAS SERIES DE ENTRENAMIENTO DIFUSO, (bas), LAS
% FUNCIONES DE ENTRENAMIENTO DEBEN TENER EL MISMO VECTOR "X" QUE
% "Y" Y DEBEN ESTAR EN COLUMNAS; F1(X) F2(X) F3(X) ...
%        UNIVERSIDAD NACIONAL AUT�NOMA DE M�XICO
%   INSTITUTO DE GEOF�SICA - FACULTAD DE INGENIER�A
%                INGENIER�A GEOF�SICA
% %     B�SQUEDA DE MEDIA Y DESVIACI�N ESTANDAR DE LA
% %     OCURRENCIA DE EVENTOS DE NATURALEZA ESTOC�STICA
% %     PARA UNA BASE DE ENTRENAMIENTO CON L�GICA DIFUSA
%  ESCRITO POR: ALAN JU�REZ Z��IGA
% % % % % % % % % % % % % % % % % % % % % % % % % % % % % %

index=find(res>limres);
base=bas(index);
m=mean(base);
s=std(base);




