// Fill out your copyright notice in the Description page of Project Settings.

#include "UVCamera.h"
#include "ConstructorHelpers.h"
#include <algorithm>
#include <sstream>
#include <chrono>

// Sets default values
AUVCamera::AUVCamera()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	Width = 480;
	Height = 300;
	FieldOfView = 90.0;
	FrameRate = 0;
	CameraId = TEXT("Cam01");
	MongoIp = TEXT("127.0.0.1");
	MongoPort = 27017;
	MongoDBName = TEXT("VisionLogger");
	FDateTime now = FDateTime::UtcNow();
	MongoCollectionName = FString::FromInt(now.GetYear()) + "_" + FString::FromInt(now.GetMonth()) + "_" + FString::FromInt(now.GetDay())
		+ "_" + FString::FromInt(now.GetHour());
	
	


	bImageSameSize = false;
	bCaptureColorImage = false;
	bInitialAsyncTask = true;
	bColorFirsttick = true;
	bColorSave = false;
	bFirstPersonView = false;

	ColorImgCaptureComp = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("ColorCapture"));
	ColorImgCaptureComp->SetupAttachment(RootComponent);
	ColorImgCaptureComp->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	ColorImgCaptureComp->TextureTarget = CreateDefaultSubobject<UTextureRenderTarget2D>(TEXT("ColorTarget"));
	ColorImgCaptureComp->TextureTarget->InitAutoFormat(Width, Height);
	ColorImgCaptureComp->FOVAngle = FieldOfView;

	ColorImgCaptureComp->SetHiddenInGame(true);
	ColorImgCaptureComp->Deactivate();

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
		ColorImgCaptureComp->SetWorldLocationAndRotation(Position, Rotation);
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
		ColorImgCaptureComp->TextureTarget->InitAutoFormat(Width, Height);

	}
	ColorImgCaptureComp->TextureTarget->InitAutoFormat(Width, Height);
	// Initializing buffers for reading images from the GPU
	ColorImage.AddZeroed(Width*Height);
	GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, FString::Printf(TEXT("Image Size: x: %i, y: %i"), Width, Height));

	if (bCaptureColorImage) {

		ColorImgCaptureComp->TextureTarget->TargetGamma = 1.4;
		GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Yellow, FString::Printf(TEXT("Gamma:%f"), GEngine->GetDisplayGamma()));
		ColorImgCaptureComp->SetHiddenInGame(false);
		ColorImgCaptureComp->Activate();
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

	if (bCaptureColorImage)
	{
		if (!bColorFirsttick && ColorPixelFence.IsFenceComplete()) {
			if (bSaveAsImage) {

				InitAsyncTask(ColorAsyncWorker, ColorImage, Stamp,CameraId+TEXT("_COLOR_"), Width, Height);
				bColorSave = true;
			}
			if (bConnectionMongo) {
				SaveImageInMongo(Stamp);

				bColorSave = true;
			}
			if (bSaveAsBsonFile) {
				SaveImageAsBson(Stamp);
				bColorSave = true;
			}
			StopAsyncTask(ColorAsyncWorker);
		}
		if (bColorFirsttick)
		{
			ProcessColorImg();
			bColorFirsttick = false;
		}
		else if (bColorSave)
		{
			ProcessColorImg();
			bColorSave = false;
		}
		UE_LOG(LogTemp, Warning, TEXT("Read Color Image"));
	}
}

void AUVCamera::ProcessColorImg()
{
	FReadSurfaceDataFlags ReadSurfaceDataFlags;
	ReadSurfaceDataFlags.SetLinearToGamma(false);
	FTextureRenderTargetResource* ColorRenderResource = ColorImgCaptureComp->TextureTarget->GameThread_GetRenderTargetResource();
	if (bCaptureViewport) {
		ColorViewport->Draw();
		//ColorViewport->ReadPixels(ColorImage);
		ReadPixels(ColorViewport, ColorImage);
	}
	if (bCaptureScencComponent)
	{
		ReadPixels(ColorRenderResource, ColorImage,ReadSurfaceDataFlags);
	}
	
	ColorPixelFence.BeginFence();
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
	bson_writer_end(writer);
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
