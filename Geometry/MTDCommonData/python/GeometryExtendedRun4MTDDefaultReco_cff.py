import FWCore.ParameterSet.Config as cms
from Geometry.MTDCommonData.defaultMTDConditionsEra_cff import MTD_DEFAULT_VERSION

reco_geometry_import_stmt = f"from Configuration.Geometry.GeometryExtended{MTD_DEFAULT_VERSION}Reco_cff import *"
exec(reco_geometry_import_stmt)
