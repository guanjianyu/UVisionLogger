// Fill out your copyright notice in the Description page of Project Settings.

#include "UVCamera.h"
#include "ConstructorHelpers.h"
#include <algorithm>
#include <sstream>
#include <chrono>
#include "Engine/StaticMeshActor.h"


// Sets default values
AUVCamera::AUVCamera()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	Width = 480;
	Height = 300;
	FieldOfView = 90.0;
	FrameRate = 0;
	ColorsUsed = 0;
	CameraId = TEXT("Cam01");
	MongoIp = TEXT("127.0.0.1");
	MongoPort = 27017;
	MongoDBName = TEXT("VisionLogger");
	FDateTime now = FDateTime::UtcNow();
	MongoCollectionName = FString::FromInt(now.GetYear()) + "_" + FString::FromInt(now.GetMonth()) + "_" + FString::FromInt(now.GetDay())
		+ "_" + FString::FromInt(now.GetHour());
	
	


	bImageSameSize = false;
	bCaptureColorImage = false;
	bCaptureMaskImage = false;
	bCaptureDepthImage = false;
	bInitialAsyncTask = true;
	bFirsttick = true;
	bColorSave = false;
	bDepthSave = false;
	bMaskSave = false;
	bFirstPersonView = false;

	ColorImgCaptureComp = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("ColorCapture"));
	ColorImgCaptureComp->SetupAttachment(RootComponent);
	ColorImgCaptureComp->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	ColorImgCaptureComp->TextureTarget = CreateDefaultSubobject<UTextureRenderTarget2D>(TEXT("ColorTarget"));
	ColorImgCaptureComp->TextureTarget->InitAutoFormat(Width, Height);
	ColorImgCaptureComp->FOVAngle = FieldOfView;

	DepthImgCaptureComp = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("DepthCapture"));
	DepthImgCaptureComp->SetupAttachment(RootComponent);
	DepthImgCaptureComp->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	DepthImgCaptureComp->TextureTarget = CreateDefaultSubobject<UTextureRenderTarget2D>(TEXT("DepthTarget"));
	DepthImgCaptureComp->TextureTarget->InitAutoFormat(Width, Height);
	DepthImgCaptureComp->FOVAngle = FieldOfView;

	MaskImgCaptureComp = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("MaskCapture"));
	MaskImgCaptureComp->SetupAttachment(RootComponent);
	MaskImgCaptureComp->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	MaskImgCaptureComp->TextureTarget = CreateDefaultSubobject<UTextureRenderTarget2D>(TEXT("MaskTarget"));
	MaskImgCaptureComp->TextureTarget->InitAutoFormat(Width, Height);
	MaskImgCaptureComp->FOVAngle = FieldOfView;

	

	// Get depth scene material for postprocessing
	ConstructorHelpers::FObjectFinder<UMaterial> MaterialDepthFinder(TEXT("Material'/UVisionLogger/ScenePlaneDepthWorldUnits.ScenePlaneDepthWorldUnits'"));
	if (MaterialDepthFinder.Object != nullptr)
	{
		//MaterialDepthInstance = UMaterialInstanceDynamic::Create(MaterialDepthFinder.Object, DepthImgCaptureComp);
		MaterialDepthInstance = (UMaterial*)MaterialDepthFinder.Object;
		if (MaterialDepthInstance != nullptr)
		{
			//PostProcess(DepthImgCaptureComp->ShowFlags);
			PostProcess(DepthImgCaptureComp->ShowFlags);
			DepthImgCaptureComp->PostProcessSettings.AddBlendable(MaterialDepthInstance, 1);
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Could not load material for depth."));
	}
	VertexColor(MaskImgCaptureComp->ShowFlags);
	ColorImgCaptureComp->SetHiddenInGame(true);
	ColorImgCaptureComp->Deactivate();

	DepthImgCaptureComp->SetHiddenInGame(true);
	DepthImgCaptureComp->Deactivate();

	MaskImgCaptureComp->SetHiddenInGame(true);
	MaskImgCaptureComp->Deactivate();

}

AUVCamera::~AUVCamera() 
{
	
	if (FileHandle)
	{
		delete FileHandle;
	}
}

// Called when the game starts or when spawned
void AUVCamera::BeginPlay()
{
	Super::BeginPlay();
	FTimerHandle TimerHandle;
	GetWorld()->GetTimerManager().SetTimer(TimerHandle, this, &AUVCamera::Initial, 1.0f, false);

}

// Called every frame
void AUVCamera::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (bFirstPersonView) {
		FVector Position = GetWorld()->GetFirstPlayerController()->PlayerCameraManager->GetCameraLocation();
		FRotator Rotation = GetWorld()->GetFirstPlayerController()->PlayerCameraManager->GetCameraRotation();
		if (bCaptureColorImage) {
			ColorImgCaptureComp->SetWorldLocationAndRotation(Position, Rotation);
		}
		if (bCaptureDepthImage) {
			DepthImgCaptureComp->SetWorldLocationAndRotation(Position, Rotation);
		}	
		if (bCaptureMaskImage) {
			MaskImgCaptureComp->SetWorldLocationAndRotation(Position, Rotation);
		}
	}
}

void AUVCamera::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
	//StopAsyncTask(ColorAsyncWorker);
	if (bConnectionMongo) {
		mongoc_collection_destroy(collection);
		mongoc_database_destroy(database);
		mongoc_client_destroy(client);
		mongoc_cleanup();
		bConnectionMongo = false;
	}
	if (bSaveAsBsonFile) {
		if (FileHandle) {
			FileHandle->Write((uint8*)buf, (int64)buflen);
		}
	}
	
}

void AUVCamera::Initial()
{
	static IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
	ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::JPEG);

	if (bSaveInMongoDB) {
		bConnectionMongo=ConnectMongo(MongoIp, MongoPort, MongoDBName, MongoCollectionName);
	}
	if (bSaveAsBsonFile) {
		//create bson write buffer
		writer = bson_writer_new(&buf, &buflen, 0, bson_realloc_ctx, NULL);
		SetFileHandle();
	}
	if (bImageSameSize)
	{
		ColorViewport = GetWorld()->GetGameViewport()->Viewport;
		Width = ColorViewport->GetRenderTargetTextureSizeXY().X;
		Height = ColorViewport->GetRenderTargetTextureSizeXY().Y;

	}

	ColorImgCaptureComp->TextureTarget->InitAutoFormat(Width, Height);
	// Initializing buffers for reading images from the GPU
	ColorImage.AddZeroed(Width*Height);
	GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, FString::Printf(TEXT("Image Size: x: %i, y: %i"), Width, Height));

	DepthImgCaptureComp->TextureTarget->InitAutoFormat(Width, Height);
	DepthImage.AddZeroed(Width*Height);

	MaskImgCaptureComp->TextureTarget->InitAutoFormat(Width, Height);
	MaskImage.AddZeroed(Width*Height);

	if (bCaptureColorImage) {

		//ColorImgCaptureComp->TextureTarget->TargetGamma = 1.4;
		ColorImgCaptureComp->TextureTarget->TargetGamma = GEngine->GetDisplayGamma();
		GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Yellow, FString::Printf(TEXT("Gamma:%f"), GEngine->GetDisplayGamma()));
		ColorImgCaptureComp->SetHiddenInGame(false);
		ColorImgCaptureComp->Activate();
	}

	if (bCaptureDepthImage) {
		DepthImgCaptureComp->TextureTarget->TargetGamma = 1.0;
		DepthImgCaptureComp->SetHiddenInGame(false);
		DepthImgCaptureComp->Activate();
	}

	if (bCaptureMaskImage) {
		if (ColorAllObjects()) {
			UE_LOG(LogTemp, Warning, TEXT("All the objects has colored"));
		}
		MaskImgCaptureComp->TextureTarget->TargetGamma = 1.0;
		MaskImgCaptureComp->SetHiddenInGame(false);
		MaskImgCaptureComp->Activate();
	}
	// Call the timer 
	SetFramerate(FrameRate);
}

void AUVCamera::SetFramerate(const float NewFramerate)
{
	if (NewFramerate > 0.0f)
	{
		// Update Camera on custom timer tick (does not guarantees the UpdateRate value,
		// since it will be eventually triggered from the game thread tick

		FTimerHandle TimerHandle;
		GetWorld()->GetTimerManager().SetTimer(TimerHandle, this, &AUVCamera::TimerTick, 1 / NewFramerate, true);

	}
}

void AUVCamera::InitAsyncTask(FAsyncTask<RawDataAsyncWorker>* AsyncWorker, TArray<FColor>& image, FDateTime Stamp, FString Name, int Width, int Height)
{

	AsyncWorker = new FAsyncTask<RawDataAsyncWorker>(image, ImageWrapper, Stamp, Name, Width, Height,MongoCollectionName,CameraId);
	AsyncWorker->StartBackgroundTask();
	AsyncWorker->EnsureCompletion();


}

void AUVCamera::CurrentAsyncTask(FAsyncTask<RawDataAsyncWorker>* AsyncWorker)
{
	if (AsyncWorker->IsDone() || bInitialAsyncTask)
	{
		bInitialAsyncTask = false;
		UE_LOG(LogTemp, Warning, TEXT("Start writing Color Image"));
		AsyncWorker->StartBackgroundTask();
		AsyncWorker->EnsureCompletion();
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[%s][%d] SKIP new task "), TEXT(__FUNCTION__), __LINE__);
	}

}

void AUVCamera::StopAsyncTask(FAsyncTask<RawDataAsyncWorker>* AsyncWorker)
{
	if (AsyncWorker)
	{
		// Wait for worker to complete before deleting it
		AsyncWorker->EnsureCompletion();
		delete AsyncWorker;
		AsyncWorker = nullptr;
	}
}

void AUVCamera::TimerTick()
{
	FDateTime Stamp = FDateTime::UtcNow();
	if (!bFirsttick && PixelFence.IsFenceComplete())
	{
		if (bSaveAsImage)
		{
			if (bCaptureColorImage)
			{
				InitAsyncTask(ColorAsyncWorker, ColorImage, Stamp, CameraId + TEXT("_COLOR_"), Width, Height);
				bColorSave = true;
			}

			if (bCaptureDepthImage)
			{
				InitAsyncTask(DepthAsyncWorker, DepthImage, Stamp, CameraId + TEXT("_Depth_"), Width, Height);
				bDepthSave = true;
			}

			if (bCaptureMaskImage)
			{
				InitAsyncTask(MaskAsyncWorker, MaskImage, Stamp, CameraId + TEXT("_Mask_"), Width, Height);
				bMaskSave = true;
			}
		}

		if (bConnectionMongo)
		{
			SaveImageInMongo(Stamp);
			if (bCaptureColorImage) { bColorSave = true; }
			if (bCaptureDepthImage) { bDepthSave = true; }
			if (bCaptureMaskImage) { bMaskSave = true; }
		}

		if (bSaveAsBsonFile)
		{
			SaveImageAsBson(Stamp);
			if (bCaptureColorImage) { bColorSave = true; }
			if (bCaptureDepthImage) { bDepthSave = true; }
			if (bCaptureMaskImage) { bMaskSave = true; }
		}
	}
	if (bFirsttick)
	{
		if (bColorSave)
		{
			ProcessColorImg();
			bColorSave = false;
			UE_LOG(LogTemp, Warning, TEXT("Read Color Image"));
		}
		if (bDepthSave)
		{
			ProcessDepthImg();
			bDepthSave = false;
			UE_LOG(LogTemp, Warning, TEXT("Read Depth Image"));
		}

		if (bMaskSave)
		{
			ProcessMaskImg();
			bMaskSave = false;
			UE_LOG(LogTemp, Warning, TEXT("Read Mask Image"));
		}
		bFirsttick = false;
	}
	if (!bFirsttick)
	{
		if (bColorSave)
		{
			ProcessColorImg();
			bColorSave = false;
		}
		if (bDepthSave)
		{
			ProcessDepthImg();
			bDepthSave = false;
		}
		if (bMaskSave)
		{
			ProcessMaskImg();
			bMaskSave = false;
		}
	}
}

void AUVCamera::ProcessColorImg()
{
	FReadSurfaceDataFlags ReadSurfaceDataFlags;
	ReadSurfaceDataFlags.SetLinearToGamma(false);
	
	if (bCaptureViewport) {
		ColorViewport->Draw();
		//ColorViewport->ReadPixels(ColorImage);
		ReadPixels(ColorViewport, ColorImage);
	}
	if (bCaptureScencComponent)
	{
		FTextureRenderTargetResource* ColorRenderResource = ColorImgCaptureComp->TextureTarget->GameThread_GetRenderTargetResource();
		ReadPixels(ColorRenderResource, ColorImage,ReadSurfaceDataFlags);
	}
	
	PixelFence.BeginFence();
}

void AUVCamera::ProcessDepthImg()
{
	FReadSurfaceDataFlags ReadSurfaceDataFlags;
	ReadSurfaceDataFlags.SetLinearToGamma(false);
	FTextureRenderTargetResource* DepthRenderResource = DepthImgCaptureComp->TextureTarget->GameThread_GetRenderTargetResource();
	ReadPixels(DepthRenderResource, DepthImage, ReadSurfaceDataFlags);
	PixelFence.BeginFence();
}

void AUVCamera::ProcessMaskImg()
{
	FReadSurfaceDataFlags ReadSurfaceDataFlags;
	ReadSurfaceDataFlags.SetLinearToGamma(false);

	FTextureRenderTargetResource* MaskRenderResource = MaskImgCaptureComp->TextureTarget->GameThread_GetRenderTargetResource();
	ReadPixels(MaskRenderResource, MaskImage, ReadSurfaceDataFlags);
	PixelFence.BeginFence();
}

void AUVCamera::ReadPixels(FViewport *& viewport, TArray<FColor>& OutImageData, FReadSurfaceDataFlags InFlags, FIntRect InRect)
{
	if (InRect == FIntRect(0, 0, 0, 0))
	{
		InRect = FIntRect(0, 0, viewport->GetSizeXY().X, viewport->GetSizeXY().Y);
	}

	// Read the render target surface data back.	
	struct FReadSurfaceContext
	{
		FRenderTarget* SrcRenderTarget;
		TArray<FColor>* OutData;
		FIntRect Rect;
		FReadSurfaceDataFlags Flags;
	};

	OutImageData.Reset();
	FReadSurfaceContext ReadSurfaceContext =
	{
		viewport,
		&OutImageData,
		InRect,
		InFlags
	};

	ENQUEUE_UNIQUE_RENDER_COMMAND_ONEPARAMETER(
		ReadSurfaceCommand,
		FReadSurfaceContext, Context, ReadSurfaceContext,
		{
			RHICmdList.ReadSurfaceData(
				Context.SrcRenderTarget->GetRenderTargetTexture(),
				Context.Rect,
				*Context.OutData,
				Context.Flags
			);
		});
}



void AUVCamera::ReadPixels(FTextureRenderTargetResource *& RenderResource, TArray<FColor>& OutImageData, FReadSurfaceDataFlags InFlags, FIntRect InRect)
{

	// Read the render target surface data back.	
	if (InRect == FIntRect(0, 0, 0, 0))
	{
		InRect = FIntRect(0, 0, RenderResource->GetSizeXY().X, RenderResource->GetSizeXY().Y);
	}
	struct FReadSurfaceContext
	{
		FRenderTarget* SrcRenderTarget;
		TArray<FColor>* OutData;
		FIntRect Rect;
		FReadSurfaceDataFlags Flags;
	};

	OutImageData.Reset();
	FReadSurfaceContext ReadSurfaceContext =
	{
		RenderResource,
		&OutImageData,
		InRect,
		InFlags,
	};

	ENQUEUE_UNIQUE_RENDER_COMMAND_ONEPARAMETER(
		ReadSurfaceCommand,
		FReadSurfaceContext, Context, ReadSurfaceContext,
		{
			RHICmdList.ReadSurfaceData(
				Context.SrcRenderTarget->GetRenderTargetTexture(),
				Context.Rect,
				*Context.OutData,
				Context.Flags
			);
		});
}
bool AUVCamera::ConnectMongo(FString & MongoIp, int & MongoPort, FString & MongoDBName, FString & MongoCollection)
{
	FString Furi_str = TEXT("mongodb://") + MongoIp + TEXT(":") + FString::FromInt(MongoPort);
	std::string uri_str(TCHAR_TO_UTF8(*Furi_str));
	std::string database_name(TCHAR_TO_UTF8(*MongoDBName));
	std::string collection_name(TCHAR_TO_UTF8(*MongoCollection));

	//Required to initialize libmongoc's internals
	mongoc_init();

	//create a new client instance
	client = mongoc_client_new(uri_str.c_str());

	//Register the application name so we can track it in the profile logs on the server
	mongoc_client_set_appname(client, "Mongo_data");

	//Get a handle on the database and collection 
	database = mongoc_client_get_database(client, database_name.c_str());
	collection = mongoc_client_get_collection(client, database_name.c_str(), collection_name.c_str());

	//connect mongo database
	if (client) {

		UE_LOG(LogTemp, Warning, TEXT("Mongo Database has been connected"));
		return true;
	}
	else {
		UE_LOG(LogTemp, Warning, TEXT("Database connection failed"));
		return false;
	}
	return false;
}

void AUVCamera::SaveImageInMongo(FDateTime Stamp)
{
	FString TimeStamp = FString::FromInt(Stamp.GetYear()) + "_" + FString::FromInt(Stamp.GetMonth()) + "_" + FString::FromInt(Stamp.GetDay())
		+ "_" + FString::FromInt(Stamp.GetHour()) + "_" + FString::FromInt(Stamp.GetMinute()) + "_" + FString::FromInt(Stamp.GetSecond()) + "_" +
		FString::FromInt(Stamp.GetMillisecond());

	bson_t *doc;
	doc = bson_new();
	BSON_APPEND_UTF8(doc, "Camera_Id", TCHAR_TO_UTF8(*CameraId));
	BSON_APPEND_UTF8(doc, "Time_Stamp", TCHAR_TO_UTF8(*TimeStamp));

	bson_t child1;
	BSON_APPEND_DOCUMENT_BEGIN(doc, "resolution", &child1);
	BSON_APPEND_INT32(&child1, "X", Height);
	BSON_APPEND_INT32(&child1, "Y", Width);
	bson_append_document_end(doc, &child1);

	if (bCaptureColorImage) {
		TArray<uint8>ImgData;
		ImgData.AddZeroed(Width*Height);
		ImageWrapper->SetRaw(ColorImage.GetData(), ColorImage.GetAllocatedSize(), Width, Height, ERGBFormat::BGRA, 8);
		ImgData = ImageWrapper->GetCompressed();

		uint8_t* imagedata = new uint8_t[ImgData.Num()];

		for (size_t i = 0; i < ImgData.Num(); i++) {
			*(imagedata + i) = ImgData[i];
		}
		bson_t child2;
		BSON_APPEND_DOCUMENT_BEGIN(doc, "Color Image", &child2);
		BSON_APPEND_UTF8(&child2, "type", TCHAR_TO_UTF8(TEXT("RGBD")));
		BSON_APPEND_BINARY(&child2, "Color_Image_Data", BSON_SUBTYPE_BINARY, imagedata, ImgData.Num());
		bson_append_document_end(doc, &child2);
		delete[] imagedata;
	}
	if (bCaptureDepthImage) {
		TArray<uint8>ImgData;
		ImgData.AddZeroed(Width*Height);
		ImageWrapper->SetRaw(DepthImage.GetData(), DepthImage.GetAllocatedSize(), Width, Height, ERGBFormat::BGRA, 8);
		ImgData = ImageWrapper->GetCompressed();

		uint8_t* imagedata = new uint8_t[ImgData.Num()];

		for (size_t i = 0; i < ImgData.Num(); i++) {
			*(imagedata + i) = ImgData[i];
		}
		bson_t child3;
		BSON_APPEND_DOCUMENT_BEGIN(doc, "Depth Image", &child3);
		BSON_APPEND_UTF8(&child3, "type", TCHAR_TO_UTF8(TEXT("Depth")));
		BSON_APPEND_BINARY(&child3, "Depth_Image_Data", BSON_SUBTYPE_BINARY, imagedata, ImgData.Num());
		bson_append_document_end(doc, &child3);
		delete[] imagedata;
	}

	if (bCaptureMaskImage) {
		TArray<uint8>ImgData;
		ImgData.AddZeroed(Width*Height);
		ImageWrapper->SetRaw(MaskImage.GetData(), MaskImage.GetAllocatedSize(), Width, Height, ERGBFormat::BGRA, 8);
		ImgData = ImageWrapper->GetCompressed();

		uint8_t* imagedata = new uint8_t[ImgData.Num()];

		for (size_t i = 0; i < ImgData.Num(); i++) {
			*(imagedata + i) = ImgData[i];
		}
		bson_t child4;
		BSON_APPEND_DOCUMENT_BEGIN(doc, "Mask Image", &child4);
		BSON_APPEND_UTF8(&child4, "type", TCHAR_TO_UTF8(TEXT("Mask")));
		BSON_APPEND_BINARY(&child4, "Mask_Image_Data", BSON_SUBTYPE_BINARY, imagedata, ImgData.Num());
		bson_append_document_end(doc, &child4);
		delete[] imagedata;
	}
	bson_error_t error;
	if (!mongoc_collection_insert_one(collection, doc, NULL, NULL, &error)) {
		fprintf(stderr, "%s\n", error.message);
	}
	else {
		UE_LOG(LogTemp, Warning, TEXT("Save Image In MongoDB"));
	}
	bson_destroy(doc);
}

void AUVCamera::SaveImageAsBson(FDateTime Stamp)
{
	bson_t *doc;
	TArray<uint8_t>Fbuf;
	bool RBuf;
	FString TimeStamp = FString::FromInt(Stamp.GetYear()) + "_" + FString::FromInt(Stamp.GetMonth()) + "_" + FString::FromInt(Stamp.GetDay())
		+ "_" + FString::FromInt(Stamp.GetHour()) + "_" + FString::FromInt(Stamp.GetMinute()) + "_" + FString::FromInt(Stamp.GetSecond()) + "_" +
		FString::FromInt(Stamp.GetMillisecond());
	RBuf = bson_writer_begin(writer, &doc);
	BSON_APPEND_UTF8(doc, "Camera_Id", TCHAR_TO_UTF8(*CameraId));
	BSON_APPEND_UTF8(doc, "Time_Stamp", TCHAR_TO_UTF8(*TimeStamp));
	bson_t child1;
	BSON_APPEND_DOCUMENT_BEGIN(doc, "resolution", &child1);
	BSON_APPEND_INT32(&child1, "X", Height);
	BSON_APPEND_INT32(&child1, "Y", Width);
	bson_append_document_end(doc, &child1);


	if (bCaptureColorImage) {
		TArray<uint8>ImgData;
		ImgData.AddZeroed(Width*Height);
		ImageWrapper->SetRaw(ColorImage.GetData(), ColorImage.GetAllocatedSize(), Width, Height, ERGBFormat::BGRA, 8);
		ImgData = ImageWrapper->GetCompressed();

		uint8_t* imagedata = new uint8_t[ImgData.Num()];

		for (size_t i = 0; i < ImgData.Num(); i++) {
			*(imagedata + i) = ImgData[i];
		}
		bson_t child2;
		BSON_APPEND_DOCUMENT_BEGIN(doc, "Color Image", &child2);
		BSON_APPEND_UTF8(&child2, "type", TCHAR_TO_UTF8(TEXT("RGBD")));
		BSON_APPEND_BINARY(&child2, "Color_Image_Data", BSON_SUBTYPE_BINARY, imagedata, ImgData.Num());
		bson_append_document_end(doc, &child2);
		delete[] imagedata;
	}
	if (bCaptureDepthImage) {
		TArray<uint8>ImgData;
		ImgData.AddZeroed(Width*Height);
		ImageWrapper->SetRaw(DepthImage.GetData(), DepthImage.GetAllocatedSize(), Width, Height, ERGBFormat::BGRA, 8);
		ImgData = ImageWrapper->GetCompressed();

		uint8_t* imagedata = new uint8_t[ImgData.Num()];

		for (size_t i = 0; i < ImgData.Num(); i++) {
			*(imagedata + i) = ImgData[i];
		}
		bson_t child3;
		BSON_APPEND_DOCUMENT_BEGIN(doc, "Depth Image", &child3);
		BSON_APPEND_UTF8(&child3, "type", TCHAR_TO_UTF8(TEXT("Depth")));
		BSON_APPEND_BINARY(&child3, "Depth_Image_Data", BSON_SUBTYPE_BINARY, imagedata, ImgData.Num());
		bson_append_document_end(doc, &child3);
		delete[] imagedata;
	}

	if (bCaptureMaskImage) {
		TArray<uint8>ImgData;
		ImgData.AddZeroed(Width*Height);
		ImageWrapper->SetRaw(MaskImage.GetData(), MaskImage.GetAllocatedSize(), Width, Height, ERGBFormat::BGRA, 8);
		ImgData = ImageWrapper->GetCompressed();

		uint8_t* imagedata = new uint8_t[ImgData.Num()];

		for (size_t i = 0; i < ImgData.Num(); i++) {
			*(imagedata + i) = ImgData[i];
		}
		bson_t child4;
		BSON_APPEND_DOCUMENT_BEGIN(doc, "Mask Image", &child4);
		BSON_APPEND_UTF8(&child4, "type", TCHAR_TO_UTF8(TEXT("Mask")));
		BSON_APPEND_BINARY(&child4, "Mask_Image_Data", BSON_SUBTYPE_BINARY, imagedata, ImgData.Num());
		bson_append_document_end(doc, &child4);
		delete[] imagedata;
	}
	bson_writer_end(writer);
}

bool AUVCamera::ColorAllObjects()
{
	uint32_t NumberOfActors = 0;

	for (TActorIterator<AStaticMeshActor> ActItr(GetWorld()); ActItr; ++ActItr)
	{

		FString ActorName = ActItr->GetName();
		if (ActorName == TEXT("Fridge_Door_Cladding_8")) {
			GEditor->SelectActor(*ActItr, true, true);
		}
		
		//FString CategoryName = ActorName.Left(7);
		if (!ObjectCategory.Contains(ActorName)) {
			++NumberOfActors;
			ObjectCategory.Add(ActorName);
		}

	}

	UE_LOG(LogTemp, Warning, TEXT("Found %d Actor Categories."), NumberOfActors);
	GenerateColors(NumberOfActors);
	for (TActorIterator<AStaticMeshActor> ActItr(GetWorld()); ActItr; ++ActItr)
	{
		FString ActorName = ActItr->GetName();
		//FString CategoryName = ActorName.Left(7);
		if (!ObjectToColor.Contains(ActorName))
		{
			check(ColorsUsed < (uint32)ObjectColors.Num());
			ObjectToColor.Add(ActorName, ColorsUsed);
			UE_LOG(LogTemp, Warning, TEXT("Adding color %d for object %s."), ColorsUsed, *ActorName);

			++ColorsUsed;
		}

		ColorObject(*ActItr, ActorName);
	}
	return true;
}

bool AUVCamera::ColorObject(AActor * Actor, const FString & name)
{
	const FColor &ObjectColor = ObjectColors[ObjectToColor[name]];
	TArray<UMeshComponent *> PaintableComponents;
	Actor->GetComponents<UMeshComponent>(PaintableComponents);
	for (auto MeshComponent : PaintableComponents)

	{
		if (MeshComponent == nullptr)
			continue;

		if (UStaticMeshComponent *StaticMeshComponent = Cast<UStaticMeshComponent>(MeshComponent))
		{
			if (UStaticMesh *StaticMesh = StaticMeshComponent->GetStaticMesh())
			{
				uint32 PaintingMeshLODIndex = 0;
				uint32 NumLODLevel = StaticMesh->RenderData->LODResources.Num();
				//check(NumLODLevel == 1);
				FStaticMeshLODResources &LODModel = StaticMesh->RenderData->LODResources[PaintingMeshLODIndex];
				StaticMeshComponent->SetLODDataCount(1, StaticMeshComponent->LODData.Num());
				FStaticMeshComponentLODInfo *InstanceMeshLODInfo = &StaticMeshComponent->LODData[PaintingMeshLODIndex];

				// PaintingMeshLODIndex + 1 is the minimum requirement, enlarge if not satisfied

				InstanceMeshLODInfo->PaintedVertices.Empty();

				InstanceMeshLODInfo->OverrideVertexColors = new FColorVertexBuffer;

				InstanceMeshLODInfo->OverrideVertexColors->InitFromSingleColor(FColor::Green, LODModel.GetNumVertices());


				uint32 NumVertices = LODModel.GetNumVertices();
				//check(InstanceMeshLODInfo->OverrideVertexColors);
				//check(NumVertices <= InstanceMeshLODInfo->OverrideVertexColors->GetNumVertices());


				for (uint32 ColorIndex = 0; ColorIndex < NumVertices; ColorIndex++)
				{
					//uint32 NumOverrideVertexColors = InstanceMeshLODInfo->OverrideVertexColors->GetNumVertices();
					//uint32 NumPaintedVertices = InstanceMeshLODInfo->PaintedVertices.Num();
					InstanceMeshLODInfo->OverrideVertexColors->VertexColor(ColorIndex) = ObjectColor;
				}
				BeginInitResource(InstanceMeshLODInfo->OverrideVertexColors);

				StaticMeshComponent->MarkRenderStateDirty();

				//UE_LOG(LogTemp, Warning, TEXT("%s:%s has %d vertices,%d,%d,%d"), *Actor->GetActorLabel(), *StaticMeshComponent->GetName(), NumVertices, InstanceMeshLODInfo->OverrideVertexColors->VertexColor(0).R, InstanceMeshLODInfo->OverrideVertexColors->VertexColor(0).G, InstanceMeshLODInfo->OverrideVertexColors->VertexColor(0).B)
			}
		}
	}
	return true;
}

void AUVCamera::GenerateColors(const uint32_t NumberOfColors)
{
	const int32_t MaxHue = 50;
	// It shifts the next Hue value used, so that colors next to each other are not very similar. This is just important for humans
	const int32_t ShiftHue = 11;
	const float MinSat = 0.65;
	const float MinVal = 0.65;

	uint32_t HueCount = MaxHue;
	uint32_t SatCount = 1;
	uint32_t ValCount = 1;

	// Compute how many different Saturations and Values are needed
	int32_t left = std::max<int32_t>(0, NumberOfColors - HueCount);
	while (left > 0)
	{
		if (left > 0)
		{
			++ValCount;
			left = NumberOfColors - SatCount * ValCount * HueCount;
		}
		if (left > 0)
		{
			++SatCount;
			left = NumberOfColors - SatCount * ValCount * HueCount;
		}
	}

	const float StepHue = 360.0f / HueCount;
	const float StepSat = (1.0f - MinSat) / std::max(1.0f, SatCount - 1.0f);
	const float StepVal = (1.0f - MinVal) / std::max(1.0f, ValCount - 1.0f);

	ObjectColors.Reserve(SatCount * ValCount * HueCount);
	UE_LOG(LogTemp, Warning, TEXT("Generating %d colors."), SatCount * ValCount * HueCount);

	FLinearColor HSVColor;
	for (uint32_t s = 0; s < SatCount; ++s)
	{
		HSVColor.G = 1.0f - s * StepSat;
		for (uint32_t v = 0; v < ValCount; ++v)
		{
			HSVColor.B = 1.0f - v * StepVal;
			for (uint32_t h = 0; h < HueCount; ++h)
			{
				HSVColor.R = ((h * ShiftHue) % MaxHue) * StepHue;
				ObjectColors.Add(HSVColor.HSVToLinearRGB().ToFColor(false));
				UE_LOG(LogTemp, Warning, TEXT("Added color %d: %d %d %d"), ObjectColors.Num(), ObjectColors.Last().R, ObjectColors.Last().G, ObjectColors.Last().B);
			}
		}
	}
}

void AUVCamera::SetFileHandle()
{
	const FString Filename =  CameraId+"_"+MongoCollectionName + TEXT(".bson");
	FString EpisodesDirPath = FPaths::ProjectDir() + TEXT("/Vision Logger/") + MongoCollectionName+ "/"+ CameraId+"/"+ TEXT("/Bson File/");
	FPaths::RemoveDuplicateSlashes(EpisodesDirPath);
	const FString FilePath = EpisodesDirPath + Filename;

	// Create logging directory path and the filehandle
	FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*EpisodesDirPath);
	FileHandle = FPlatformFileManager::Get().GetPlatformFile().OpenWrite(*FilePath, true);
}

void AUVCamera::BasicSetting(FEngineShowFlags & ShowFlags)
{
	ShowFlags = FEngineShowFlags(EShowFlagInitMode::ESFIM_All0);
	ShowFlags.SetRendering(true);
	ShowFlags.SetStaticMeshes(true);

	ShowFlags.SetMaterials(true); // Important for the correctness of tree leaves.
}

void AUVCamera::VertexColor(FEngineShowFlags & ShowFlags)
{
	FEngineShowFlags PreviousShowFlags(ShowFlags); // Store previous ShowFlags
	ApplyViewMode(VMI_Lit, true, ShowFlags);

	// From MeshPaintEdMode.cpp:2942
	ShowFlags.SetMaterials(false);
	ShowFlags.SetLighting(false);
	ShowFlags.SetBSPTriangles(true);
	ShowFlags.SetVertexColors(true);
	ShowFlags.SetPostProcessing(false);
	ShowFlags.SetHMDDistortion(false);
	ShowFlags.SetTonemapper(false); // This won't take effect here

	GVertexColorViewMode = EVertexColorViewMode::Color;
	SetVisibility(ShowFlags, PreviousShowFlags); // Store the visibility of the scene, such as folliage and landscape.
}

void AUVCamera::PostProcess(FEngineShowFlags & ShowFlags)
{
	FEngineShowFlags PreviousShowFlags(ShowFlags); // Store previous ShowFlags
	BasicSetting(ShowFlags);
	// These are minimal setting
	ShowFlags.SetPostProcessing(true);
	ShowFlags.SetPostProcessMaterial(true);
	// ShowFlags.SetVertexColors(true); // This option will change object material to vertex color material, which don't produce surface normal

	GVertexColorViewMode = EVertexColorViewMode::Color;
	SetVisibility(ShowFlags, PreviousShowFlags); // Store the visibility of the scene, such as folliage and landscape.
}

void AUVCamera::SetVisibility(FEngineShowFlags & Target, FEngineShowFlags & Source)
{
	Target.SetStaticMeshes(Source.StaticMeshes);
	Target.SetLandscape(Source.Landscape);

	Target.SetInstancedFoliage(Source.InstancedFoliage);
	Target.SetInstancedGrass(Source.InstancedGrass);
	Target.SetInstancedStaticMeshes(Source.InstancedStaticMeshes);

	Target.SetSkeletalMeshes(Source.SkeletalMeshes);
}
